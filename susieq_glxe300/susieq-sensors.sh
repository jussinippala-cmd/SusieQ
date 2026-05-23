#!/bin/sh
#
# susieq-sensors.sh — lukee cockpit-datan, postaa Supabaseen, lähettää ntfy-hälytykset
# Asennus: /usr/bin/susieq-sensors.sh
# Cron:    * * * * * /usr/bin/susieq-sensors.sh
#

. /etc/susieq.env

# Varmista pakolliset env-muuttujat
: "${SUPABASE_URL:?susieq.env: SUPABASE_URL puuttuu}"
: "${SUPABASE_SERVICE_KEY:?susieq.env: SUPABASE_SERVICE_KEY puuttuu}"
: "${NTFY_CHANNEL:?susieq.env: NTFY_CHANNEL puuttuu}"

# Vain yksi instanssi kerrallaan (atomic mkdir-lukko)
LOCK="/tmp/susieq-sensors.lock"
if ! mkdir "$LOCK" 2>/dev/null; then
    exit 0
fi
trap 'rm -rf "$LOCK"' EXIT INT TERM

COCKPIT_URL="http://192.168.8.100/data"
LAST_OK_FILE="/tmp/susieq_last_ok"
OFFLINE_FLAG="/tmp/susieq_offline_notified"
NOW=$(date +%s)

# Alusta aikaleima ensimmäiselle ajolle
[ -f "$LAST_OK_FILE" ] || echo "$NOW" > "$LAST_OK_FILE"

# Yölepo-suojaus: jos modeemi käynnistynyt alle 10 min sitten, nollataan
# aikaleima hiljaa — ei lähetetä väärää offline-hälytystä herätyksen jälkeen
UPTIME_S=$(awk '{print int($1)}' /proc/uptime)
if [ "$UPTIME_S" -lt 600 ]; then
    echo "$NOW" > "$LAST_OK_FILE"
    rm -f "$OFFLINE_FLAG"
    exit 0
fi

# ── 1. Hae sensordata cockpitilta ────────────────────────────────────
DATA=$(curl -s --connect-timeout 5 --max-time 10 "$COCKPIT_URL")
if [ $? -ne 0 ] || [ -z "$DATA" ]; then
    logger -t susieq-sensors "ERROR: ei yhteyttä cockpittiin (192.168.8.100)"
    LAST_OK=$(cat "$LAST_OK_FILE" 2>/dev/null || echo "$NOW")
    ELAPSED=$((NOW - LAST_OK))
    [ "$ELAPSED" -lt 0 ] && ELAPSED=0
    if [ "$ELAPSED" -gt 300 ] && [ ! -f "$OFFLINE_FLAG" ]; then
        curl -s --connect-timeout 5 --max-time 10 \
             -H "Priority: high" \
             -d "SusieQ cockpit offline — ei vastausta ${ELAPSED}s" \
             "https://ntfy.sh/${NTFY_CHANNEL}"
        touch "$OFFLINE_FLAG"
    fi
    exit 1
fi

# Validoi JSON ennen Supabase-inserttiä
if ! echo "$DATA" | python3 -c "import sys,json; json.load(sys.stdin)" 2>/dev/null; then
    logger -t susieq-sensors "ERROR: cockpit palautti virheellistä JSON:ia"
    exit 1
fi

# Onnistui — päivitä aikaleima ja ilmoita paluusta jos oltiin offline
echo "$NOW" > "$LAST_OK_FILE"
if [ -f "$OFFLINE_FLAG" ]; then
    curl -s --connect-timeout 5 --max-time 10 \
         -H "Priority: default" \
         -d "SusieQ cockpit takaisin online ✓" \
         "https://ntfy.sh/${NTFY_CHANNEL}"
    rm -f "$OFFLINE_FLAG"
fi

# ── 2. Postaa Supabaseen ─────────────────────────────────────────────
HTTP_CODE=$(curl -s --connect-timeout 5 --max-time 15 \
    -o /dev/null -w "%{http_code}" \
    -X POST "${SUPABASE_URL}/rest/v1/sensor_readings" \
    -H "apikey: ${SUPABASE_SERVICE_KEY}" \
    -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}" \
    -H "Content-Type: application/json" \
    -H "Prefer: return=minimal" \
    -d "{\"data\": ${DATA}}")

if [ "$HTTP_CODE" != "201" ]; then
    logger -t susieq-sensors "ERROR: Supabase POST epäonnistui (HTTP $HTTP_CODE)"
else
    logger -t susieq-sensors "OK: data lähetetty"
fi

# ── 3. Hälytykset ────────────────────────────────────────────────────
SUSIEQ_DATA="$DATA" python3 <<PYEOF
import json, subprocess, os, math, time

data = json.loads(os.environ['SUSIEQ_DATA'])

NTFY     = os.environ.get('NTFY_CHANNEL', 'SusieQpurjevene')
HOME_LAT = float(os.environ.get('HOME_LAT', '61.515544'))
HOME_LON = float(os.environ.get('HOME_LON', '23.790427'))

def ntfy(msg, priority='default'):
    subprocess.run(
        ['curl', '-s', '--connect-timeout', '5', '--max-time', '10',
         '-H', f'Priority: {priority}', '-d', msg,
         f'https://ntfy.sh/{NTFY}'],
        capture_output=True)

def haversine_m(lat1, lon1, lat2, lon2):
    R = 6371000
    p = math.pi / 180
    a = (math.sin((lat2-lat1)*p/2)**2
         + math.cos(lat1*p)*math.cos(lat2*p)*math.sin((lon2-lon1)*p/2)**2)
    return 2 * R * math.asin(math.sqrt(a))

def read_f(path, default=''):
    try: return open(path).read().strip()
    except: return default

def write_f(path, val):
    open(path, 'w').write(str(val))

def remove_f(path):
    try: os.remove(path)
    except: pass

# ── GPS: satamasta lähtö ja paluu ─────────────────────────────────────
gps = data.get('gps', {})
if gps.get('fix') and gps.get('lat') is not None and gps.get('lon') is not None:
    dist = haversine_m(gps['lat'], gps['lon'], HOME_LAT, HOME_LON)
    state = read_f('/tmp/susieq_state', 'home')

    if dist > 300:
        write_f('/tmp/susieq_near_count', 0)
        if state == 'home':
            write_f('/tmp/susieq_state', 'away')
            ntfy('SusieQ lähtenyt satamasta ⛵', 'high')
    else:
        count = int(read_f('/tmp/susieq_near_count', '0')) + 1
        write_f('/tmp/susieq_near_count', count)
        if count >= 3 and state == 'away':
            write_f('/tmp/susieq_state', 'home')
            ntfy('SusieQ takaisin satamassa ⚓', 'default')

# ── Akku ──────────────────────────────────────────────────────────────
bat = data.get('battery', {})
if bat.get('valid') and bat.get('voltage') is not None:
    v = bat['voltage']
    if v < 12.0:
        if not os.path.exists('/tmp/susieq_volt_notified'):
            ntfy(f'Akku {v:.1f} V — lataa pikaisesti!', 'urgent')
            write_f('/tmp/susieq_volt_notified', v)
    elif v >= 12.4:
        remove_f('/tmp/susieq_volt_notified')


# ── Polttoaine (25 L max) ─────────────────────────────────────────────
fuel = data.get('fuel', {})
if fuel.get('valid') and fuel.get('liters') is not None:
    pct = (fuel['liters'] / 25.0) * 100
    if pct < 20:
        low_since = read_f('/tmp/susieq_fuel_low_since', '')
        if not low_since:
            write_f('/tmp/susieq_fuel_low_since', int(time.time()))
        else:
            try:
                if int(time.time()) - int(low_since) >= 300 and not os.path.exists('/tmp/susieq_fuel_notified'):
                    ntfy(f'Polttoaine {pct:.0f}% ({fuel["liters"]:.1f} L)', 'default')
                    write_f('/tmp/susieq_fuel_notified', pct)
            except ValueError:
                write_f('/tmp/susieq_fuel_low_since', int(time.time()))
    elif pct >= 20:
        remove_f('/tmp/susieq_fuel_low_since')
        if pct >= 30:
            remove_f('/tmp/susieq_fuel_notified')

PYEOF

#!/bin/sh
#
# susieq-sensors.sh — lukee cockpit-datan, postaa Supabaseen, lähettää ntfy-hälytykset
# Asennus: /usr/bin/susieq-sensors.sh
# Cron:    * * * * * /usr/bin/susieq-sensors.sh
#

. /etc/susieq.env

COCKPIT_URL="http://192.168.8.100/data"

# ── 1. Hae sensordata cockpitilta ────────────────────────────────────
DATA=$(curl -sf --connect-timeout 5 --max-time 10 "$COCKPIT_URL")
if [ $? -ne 0 ] || [ -z "$DATA" ]; then
    logger -t susieq-sensors "ERROR: ei yhteyttä cockpittiin (192.168.8.100)"
    exit 1
fi

# ── 2. Postaa Supabaseen ─────────────────────────────────────────────
HTTP_CODE=$(curl -sf --connect-timeout 5 --max-time 15 \
    -o /dev/null -w "%{http_code}" \
    -X POST "${SUPABASE_URL}/rest/v1/sensor_readings" \
    -H "apikey: ${SUPABASE_SERVICE_KEY}" \
    -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}" \
    -H "Content-Type: application/json" \
    -H "Prefer: return=minimal" \
    -d "{\"data\": ${DATA}}")

if [ "$HTTP_CODE" != "201" ]; then
    logger -t susieq-sensors "ERROR: Supabase POST epäonnistui (HTTP $HTTP_CODE)"
    exit 1
fi

logger -t susieq-sensors "OK: data lähetetty"

# ── 3. Hälytykset ────────────────────────────────────────────────────
echo "$DATA" | python3 - <<PYEOF
import sys, json, subprocess, os, math

data = json.loads(sys.stdin.read())

NTFY     = os.environ.get('NTFY_CHANNEL', 'SusieQpurjevene')
HOME_LAT = float(os.environ.get('HOME_LAT', '61.515544'))
HOME_LON = float(os.environ.get('HOME_LON', '23.790427'))

def ntfy(msg, priority='default'):
    subprocess.run(
        ['curl', '-sf', '-H', f'Priority: {priority}', '-d', msg,
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
if gps.get('fix'):
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
if bat.get('valid'):
    v = bat['voltage']
    if v < 12.0:
        if not os.path.exists('/tmp/susieq_volt_notified'):
            ntfy(f'Akku {v:.1f} V — lataa pikaisesti!', 'urgent')
            write_f('/tmp/susieq_volt_notified', v)
    elif v >= 12.4:
        remove_f('/tmp/susieq_volt_notified')

# ── Vesitankki (15 L max) ─────────────────────────────────────────────
water = data.get('water', {})
if water.get('valid'):
    pct = (water['liters'] / 15.0) * 100
    if pct < 20:
        if not os.path.exists('/tmp/susieq_water_notified'):
            ntfy(f'Vesitankki {pct:.0f}% ({water["liters"]:.1f} L)', 'default')
            write_f('/tmp/susieq_water_notified', pct)
    elif pct >= 30:
        remove_f('/tmp/susieq_water_notified')

# ── Polttoaine (25 L max) ─────────────────────────────────────────────
fuel = data.get('fuel', {})
if fuel.get('valid'):
    pct = (fuel['liters'] / 25.0) * 100
    if pct < 20:
        if not os.path.exists('/tmp/susieq_fuel_notified'):
            ntfy(f'Polttoaine {pct:.0f}% ({fuel["liters"]:.1f} L)', 'default')
            write_f('/tmp/susieq_fuel_notified', pct)
    elif pct >= 30:
        remove_f('/tmp/susieq_fuel_notified')

PYEOF

#!/bin/sh
#
# susieq-sleep.sh — Laittaa 4G-radion lepotilaan jos vene on kotisatamassa
# Asennus: /usr/bin/susieq-sleep.sh  (chmod +x)
# Cron:    0 22 * * * /usr/bin/susieq-sleep.sh
#
# Tarkastaa /mnt/sda1/susieq-state/susieq_state ennen sammutusta:
#   away  → vene merellä, ohitetaan lepotila
#   home  → satamassa, CFUN=4
#   (ei tiedostoa) → oletus: satamassa, CFUN=4
#
# Tila kirjoitetaan SD-kortille susieq-sensors.sh:ssä (commit 301f1a3),
# ei /tmp:iin, koska /tmp on tmpfs ja tyhjenee modeemin rebootissa.

. /etc/susieq.env

STATE=$(cat /mnt/sda1/susieq-state/susieq_state 2>/dev/null)

if [ "$STATE" = "away" ]; then
    logger -t susieq-sleep "Ohitettu: vene poissa kotisatamasta — 4G pysyy päällä"
    exit 0
fi

if [ -f /etc/susieq_no_sleep ]; then
    logger -t susieq-sleep "Ohitettu: manuaalinen yliajo (/etc/susieq_no_sleep) — 4G pysyy päällä"
    rm -f /etc/susieq_no_sleep
    exit 0
fi

# Tarkista Supabase-asetus (fallback: nukutaan jos ei vastausta)
SLEEP_ENABLED=$(curl -s --connect-timeout 5 --max-time 10 \
  "${SUPABASE_URL}/rest/v1/modem_settings?id=eq.1&select=sleep_enabled" \
  -H "apikey: ${SUPABASE_SERVICE_KEY}" | \
  python3 -c "import sys,json; d=json.load(sys.stdin); print(d[0]['sleep_enabled'])" 2>/dev/null)
if [ "$SLEEP_ENABLED" = "False" ]; then
    logger -t susieq-sleep "Ohitettu: lepotila poistettu susieq.net-asetuksista"
    exit 0
fi

logger -t susieq-sleep "Vene kotisatamassa (tila: ${STATE:-home}) — 4G lepotilaan"

STATE_DIR=/mnt/sda1/susieq-state
DECISION_LOG="$STATE_DIR/sleep_decision.log"
SLEEP_UNTIL="$STATE_DIR/esp32_sleep_until"

if ! gl_modem AT AT+CFUN=4; then
    logger -t susieq-sleep "VIRHE: CFUN=4 epäonnistui — ESP32 jää hereille"
    echo "$(date -Iseconds) modem=failed esp32=skipped" >> "$DECISION_LOG"
    exit 1
fi
touch /tmp/susieq_slept

# ESP32 nukkuu vain jos modeemi tosiasiassa nukkui. Kesto lasketaan klo 05:45:een.
# Laskenta awk:lla eikä shell-aritmetiikalla: ash tulkitsee "08" ja "09"
# oktaaliluvuiksi, jolloin $(( $(date +%H) * 3600 )) hajoaisi aamukahdeksalta.
SLEEP_S=$(date +%H:%M:%S | awk -F: '{
    n = $1*3600 + $2*60 + $3
    t = 5*3600 + 45*60
    s = 86400 - n + t
    if (s > 86400) s -= 86400
    if (s > 28800) s = 28800
    print s
}')

RESP=$(curl -s --connect-timeout 5 --max-time 10 \
    -o /tmp/susieq_sleep_resp -w "%{http_code}" \
    -X POST "http://192.168.8.100/sleep?seconds=${SLEEP_S}")
BODY=$(cat /tmp/susieq_sleep_resp 2>/dev/null)
rm -f /tmp/susieq_sleep_resp

if [ "$RESP" = "200" ]; then
    WAKE_AT=$(( $(date +%s) + SLEEP_S ))
    echo "$WAKE_AT" > "$SLEEP_UNTIL"
    logger -t susieq-sleep "ESP32 nukkumaan ${SLEEP_S}s"
    echo "$(date -Iseconds) modem=slept esp32=ok seconds=$SLEEP_S http=200" >> "$DECISION_LOG"
else
    # Fail-open: ESP32 jää hereille, ei merkkitiedostoa. Offline-vaimennus
    # (susieq-sensors.sh) ei siis käynnisty, ja aito valvonta jatkuu.
    logger -t susieq-sleep "ESP32 ei nukahtanut (http=$RESP) — jää hereille"
    echo "$(date -Iseconds) modem=slept esp32=fail http=$RESP body=$BODY" >> "$DECISION_LOG"
fi

# Päätösloki kirjoitetaan SD-kortille, koska modeemin logread-putki katkeaa
# öisin (aukot 22:00-02:51 ja 02:59-05:52 havaittu 2026-08-23) eikä loggerin
# viesteihin voi luottaa juuri sinä yönä kun jotain menee pieleen.
tail -50 "$DECISION_LOG" > "$DECISION_LOG.tmp" && mv "$DECISION_LOG.tmp" "$DECISION_LOG"

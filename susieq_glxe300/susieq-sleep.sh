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
gl_modem AT AT+CFUN=4 && touch /tmp/susieq_slept

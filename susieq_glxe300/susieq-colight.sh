#!/bin/sh
#
# susieq-colight.sh — CoLight-kytkinpaneelin komentojono + tilan taustapäivitys
# Asennus: /usr/bin/susieq-colight.sh (chmod +x)
# Käynnistys: /etc/init.d/susieq-colight (procd, jatkuva prosessi — EI cron,
# koska pollausväli on 2s eikä croni tue sitä)
#

. /etc/susieq.env

: "${SUPABASE_URL:?susieq.env: SUPABASE_URL puuttuu}"
: "${SUPABASE_SERVICE_KEY:?susieq.env: SUPABASE_SERVICE_KEY puuttuu}"

COCKPIT_URL="http://192.168.8.100"
REFRESH_INTERVAL_S=120
LAST_REFRESH=0

while true; do
    NOW=$(date +%s)

    ROW=$(curl -sf --connect-timeout 5 --max-time 10 \
        -H "apikey: ${SUPABASE_SERVICE_KEY}" \
        -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}" \
        "${SUPABASE_URL}/rest/v1/switch_commands?status=eq.pending&select=id,channel,action&order=created_at.asc&limit=1")

    if [ -n "$ROW" ] && [ "$ROW" != "[]" ]; then
        SUSIEQ_ROW="$ROW" SUPABASE_URL="$SUPABASE_URL" SUPABASE_SERVICE_KEY="$SUPABASE_SERVICE_KEY" \
        COCKPIT_URL="$COCKPIT_URL" python3 <<'PYEOF'
import json, subprocess, os

row = json.loads(os.environ['SUSIEQ_ROW'])[0]
cmd_id  = row['id']
channel = row['channel']
action  = row['action']

SUPABASE_URL = os.environ['SUPABASE_URL']
SUPABASE_KEY = os.environ['SUPABASE_SERVICE_KEY']
COCKPIT_URL  = os.environ['COCKPIT_URL']

def patch_command(status):
    subprocess.run([
        'curl', '-s', '--connect-timeout', '5', '--max-time', '10', '-X', 'PATCH',
        '-H', f'apikey: {SUPABASE_KEY}',
        '-H', f'Authorization: Bearer {SUPABASE_KEY}',
        '-H', 'Content-Type: application/json',
        '-d', json.dumps({'status': status}),
        f'{SUPABASE_URL}/rest/v1/switch_commands?id=eq.{cmd_id}'
    ], capture_output=True)

def insert_state(channels):
    subprocess.run([
        'curl', '-s', '--connect-timeout', '5', '--max-time', '10', '-X', 'POST',
        '-H', f'apikey: {SUPABASE_KEY}',
        '-H', f'Authorization: Bearer {SUPABASE_KEY}',
        '-H', 'Content-Type: application/json',
        '-H', 'Prefer: return=minimal',
        '-d', json.dumps({'data': {'valid': True, 'channels': channels}}),
        f'{SUPABASE_URL}/rest/v1/switch_state'
    ], capture_output=True)

patch_command('processing')

result = subprocess.run([
    'curl', '-sf', '--connect-timeout', '3', '--max-time', '10', '-X', 'POST',
    '-H', 'Content-Type: application/json',
    f'{COCKPIT_URL}/colight?channel={channel}&action={action}'
], capture_output=True, text=True)

if result.returncode != 0 or not result.stdout:
    patch_command('error')
else:
    try:
        body = json.loads(result.stdout)
    except ValueError:
        body = {'success': False}
    if body.get('success'):
        insert_state(body['state'])
        patch_command('done')
    else:
        patch_command('error')
PYEOF
        LAST_REFRESH=$NOW

    elif [ $((NOW - LAST_REFRESH)) -ge $REFRESH_INTERVAL_S ]; then
        SUPABASE_URL="$SUPABASE_URL" SUPABASE_SERVICE_KEY="$SUPABASE_SERVICE_KEY" \
        COCKPIT_URL="$COCKPIT_URL" python3 <<'PYEOF'
import json, subprocess, os

SUPABASE_URL = os.environ['SUPABASE_URL']
SUPABASE_KEY = os.environ['SUPABASE_SERVICE_KEY']
COCKPIT_URL  = os.environ['COCKPIT_URL']

result = subprocess.run([
    'curl', '-sf', '--connect-timeout', '3', '--max-time', '10',
    f'{COCKPIT_URL}/colight/state'
], capture_output=True, text=True)

if result.returncode == 0 and result.stdout:
    try:
        body = json.loads(result.stdout)
    except ValueError:
        body = {'success': False}
    if body.get('success'):
        subprocess.run([
            'curl', '-s', '--connect-timeout', '5', '--max-time', '10', '-X', 'POST',
            '-H', f'apikey: {SUPABASE_KEY}',
            '-H', f'Authorization: Bearer {SUPABASE_KEY}',
            '-H', 'Content-Type: application/json',
            '-H', 'Prefer: return=minimal',
            '-d', json.dumps({'data': {'valid': True, 'channels': body['state']}}),
            f'{SUPABASE_URL}/rest/v1/switch_state'
        ], capture_output=True)
PYEOF
        LAST_REFRESH=$NOW
    fi

    sleep 2
done

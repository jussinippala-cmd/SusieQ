#!/bin/sh
#
# susieq-camera.sh — kamerakomentojen pollausskripti
# Asennus: /usr/bin/susieq-camera.sh
# Cron:    * * * * * /usr/bin/susieq-camera.sh  (+ sleep 10..50 -rivit, 6/min)
#
# camera_commands: dashboard lisää rivin {camera: masto1|masto2, status: pending},
# tämä skripti hakee kuvan kameralta, lataa sen yksityiseen kamerat-bucketiin
# ja kirjoittaa image_url-kenttään objektin polun (esim. masto1/1790000000.jpg).
# Dashboard muodostaa polusta signed URL:n.

# set -a: Python-osa lukee muuttujat os.environista
set -a
. /etc/susieq.env
set +a

: "${SUPABASE_URL:?susieq.env: SUPABASE_URL puuttuu}"
: "${SUPABASE_SERVICE_KEY:?susieq.env: SUPABASE_SERVICE_KEY puuttuu}"

# Hae pending-komennot
RESPONSE=$(curl -sf --connect-timeout 5 --max-time 10 \
    -H "apikey: ${SUPABASE_SERVICE_KEY}" \
    -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}" \
    "${SUPABASE_URL}/rest/v1/camera_commands?status=eq.pending&select=id,camera&order=id")

[ -z "$RESPONSE" ] || [ "$RESPONSE" = "[]" ] && exit 0

# Heredoc varaa stdinin Python-koodille → komennot välitetään ympäristömuuttujassa
export RESPONSE
python3 - <<'PYEOF'
import json, subprocess, os, time

SUPABASE_URL = os.environ['SUPABASE_URL']
SUPABASE_KEY = os.environ['SUPABASE_SERVICE_KEY']
BUCKET       = 'kamerat'
CAM_IP       = {
    'masto1': os.environ.get('MASTO1_IP', '192.168.8.101'),  # keula
    'masto2': os.environ.get('MASTO2_IP', '192.168.8.102'),  # perä
}

def log(msg):
    subprocess.run(['logger', '-t', 'susieq-camera', msg])

def patch(cmd_id, payload):
    subprocess.run([
        'curl', '-sf', '--max-time', '10', '-X', 'PATCH',
        '-H', f'apikey: {SUPABASE_KEY}',
        '-H', f'Authorization: Bearer {SUPABASE_KEY}',
        '-H', 'Content-Type: application/json',
        '-d', json.dumps(payload),
        f'{SUPABASE_URL}/rest/v1/camera_commands?id=eq.{cmd_id}'
    ], capture_output=True)

for cmd in json.loads(os.environ['RESPONSE']):
    cmd_id = cmd['id']
    camera = cmd['camera']
    ip = CAM_IP.get(camera)
    if not ip:
        patch(cmd_id, {'status': 'error'})
        log(f'#{cmd_id}: tuntematon kamera {camera}')
        continue

    patch(cmd_id, {'status': 'in_progress'})

    ts    = int(time.time())
    fname = f'/tmp/susieq_{camera}_{ts}.jpg'

    r = subprocess.run([
        'curl', '-sf', '--connect-timeout', '5', '--max-time', '15',
        f'http://{ip}/capture', '-o', fname
    ])

    if r.returncode != 0 or not os.path.exists(fname) or os.path.getsize(fname) < 100:
        patch(cmd_id, {'status': 'error'})
        log(f'#{cmd_id}: {camera} ({ip}) ei vastannut, curl={r.returncode}')
        try: os.remove(fname)
        except OSError: pass
        continue

    obj_path = f'{camera}/{ts}.jpg'
    up = subprocess.run([
        'curl', '-sf', '--max-time', '30', '-X', 'POST',
        '-H', f'apikey: {SUPABASE_KEY}',
        '-H', f'Authorization: Bearer {SUPABASE_KEY}',
        '-H', 'Content-Type: image/jpeg',
        '--data-binary', f'@{fname}',
        f'{SUPABASE_URL}/storage/v1/object/{BUCKET}/{obj_path}'
    ], capture_output=True)

    os.remove(fname)

    if up.returncode != 0:
        patch(cmd_id, {'status': 'error'})
        log(f'#{cmd_id}: storage-lataus epäonnistui, curl={up.returncode}')
        continue

    patch(cmd_id, {'status': 'done', 'image_url': obj_path})
    log(f'#{cmd_id}: {obj_path} ok')

PYEOF

#!/bin/sh
#
# susieq-camera.sh — GL-XE300 kamerakomentojen pollaussk ripti
# Asennus: /usr/bin/susieq-camera.sh
# Cron:    * * * * * /usr/bin/susieq-camera.sh
#

SUPABASE_URL="https://wrcylgkeduujvoilqquz.supabase.co"
SUPABASE_KEY="sb_publishable_H51PHTwMasoUY1m7FnEKgQ_O5ceFj8O"
BUCKET="kamerat"

CAM_KEULA="192.168.8.101"
CAM_PERA="192.168.8.102"

cam_ip() {
    case "$1" in
        keula) echo "$CAM_KEULA" ;;
        pera)  echo "$CAM_PERA" ;;
        *)     echo "" ;;
    esac
}

# Hae pending-komennot
RESPONSE=$(curl -sf --connect-timeout 5 \
    -H "apikey: $SUPABASE_KEY" \
    -H "Authorization: Bearer $SUPABASE_KEY" \
    "${SUPABASE_URL}/rest/v1/camera_commands?status=eq.pending&select=id,camera")

[ -z "$RESPONSE" ] || [ "$RESPONSE" = "[]" ] && exit 0

# Parsitaan JSON Pythonilla (saatavilla GL-XE300:ssa)
echo "$RESPONSE" | python3 - <<'PYEOF'
import sys, json, subprocess, os, time

SUPABASE_URL = "https://wrcylgkeduujvoilqquz.supabase.co"
SUPABASE_KEY = "sb_publishable_H51PHTwMasoUY1m7FnEKgQ_O5ceFj8O"
BUCKET       = "kamerat"
CAM_IP       = {"keula": "192.168.8.101", "pera": "192.168.8.102"}

def patch(cmd_id, payload):
    subprocess.run([
        "curl", "-sf", "-X", "PATCH",
        "-H", f"apikey: {SUPABASE_KEY}",
        "-H", f"Authorization: Bearer {SUPABASE_KEY}",
        "-H", "Content-Type: application/json",
        "-d", json.dumps(payload),
        f"{SUPABASE_URL}/rest/v1/camera_commands?id=eq.{cmd_id}"
    ], capture_output=True)

data = json.loads(sys.stdin.read())

for cmd in data:
    cmd_id = cmd["id"]
    camera = cmd["camera"]
    ip = CAM_IP.get(camera)
    if not ip:
        patch(cmd_id, {"status": "error"})
        continue

    patch(cmd_id, {"status": "processing"})

    ts    = int(time.time())
    fname = f"/tmp/susieq_{camera}_{ts}.jpg"

    # Kuvan kaappaus kameralta
    r = subprocess.run([
        "curl", "-sf", "--connect-timeout", "5", "--max-time", "15",
        f"http://{ip}/capture", "-o", fname
    ])

    if r.returncode != 0 or not os.path.exists(fname) or os.path.getsize(fname) < 100:
        patch(cmd_id, {"status": "error"})
        try: os.remove(fname)
        except: pass
        continue

    # Lataus Supabase Storageen
    obj_path = f"{camera}/{ts}.jpg"
    up = subprocess.run([
        "curl", "-sf", "-X", "POST",
        "-H", f"apikey: {SUPABASE_KEY}",
        "-H", f"Authorization: Bearer {SUPABASE_KEY}",
        "-H", "Content-Type: image/jpeg",
        "--data-binary", f"@{fname}",
        f"{SUPABASE_URL}/storage/v1/object/{BUCKET}/{obj_path}"
    ], capture_output=True)

    os.remove(fname)

    if up.returncode != 0:
        patch(cmd_id, {"status": "error"})
        continue

    image_url = f"{SUPABASE_URL}/storage/v1/object/public/{BUCKET}/{obj_path}"
    patch(cmd_id, {"status": "done", "image_url": image_url})

PYEOF

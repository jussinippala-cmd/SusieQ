# SusieQ Remote Monitoring — Design Spec
**Päivämäärä:** 2026-04-19  
**Status:** Hyväksytty

## Tavoite

Mahdollistaa SusieQ:n sensoridatan ja mastokamerakuvien lukeminen mistä tahansa internetyhteydellä — ilman suoraa WiFi-yhteyttä veneeseen. Netlify-sivu toimii etädashboardina, Supabase pilvivarastona.

## Arkkitehtuuri

```
VENE
┌─────────────────────────────────────────────────────┐
│  ESP32 cockpit (192.168.4.1)                        │
│    └── WiFi AP: SusieQ-Data                         │
│                                                     │
│  Mastokamera 1 + 2 (ESP32-CAM)                      │
│    └── WiFi STA → SusieQ-Net (GL-XE300:n AP)        │
│                                                     │
│  GL-XE300                                           │
│    ├── 4G modem → internet (WAN)                    │
│    ├── WiFi STA → SusieQ-Data (lukee sensorit)      │
│    └── WiFi AP: SusieQ-Net (mastokamerat yhdistää)  │
│                                                     │
│  Cron 60s: susieq-sensors.sh                        │
│  Cron 10s: susieq-camera.sh                         │
└─────────────────────────────────────────────────────┘
          │ 4G / HTTPS
          ▼
┌─────────────────────────┐
│  Supabase               │
│  ├── sensor_readings    │
│  ├── camera_commands    │
│  └── Storage: kamerat   │
└─────────────────────────┘
          │
          ▼
┌─────────────────────────┐
│  Netlify                │
│  └── susieq.app         │
└─────────────────────────┘
```

## Komponentit

### 1. GL-XE300 — verkko

- **WAN:** 4G LTE SIM-kortti
- **WiFi STA:** yhdistää `SusieQ-Data`-AP:iin (192.168.4.x) sensoridatan lukemista varten
- **WiFi AP:** `SusieQ-Net` — mastokamerat yhdistävät tähän STA-modessa
- GL-XE300:n WiFi-radio tukee samanaikaista AP+STA-modea (OpenWRT virtual interface) — **huom:** single-radio-rajoitus vaatii että SusieQ-Data ja SusieQ-Net käyttävät samaa WiFi-kanavaa. Verifioitava asennuksessa.

### 2. GL-XE300 — skriptit (OpenWRT)

**`/usr/bin/susieq-sensors.sh`** — cron 60s:
```sh
#!/bin/sh
. /etc/susieq.env
DATA=$(curl -sf --max-time 10 http://192.168.4.1/data) || exit 1
curl -sf --max-time 10 \
  -X POST "${SUPABASE_URL}/rest/v1/sensor_readings" \
  -H "apikey: ${SUPABASE_SERVICE_KEY}" \
  -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}" \
  -H "Content-Type: application/json" \
  -d "{\"data\": $DATA}"
```

**`/usr/bin/susieq-camera.sh`** — cron 10s (on-demand-malli):
```sh
#!/bin/sh
. /etc/susieq.env
# Hae pending-komennot
COMMANDS=$(curl -sf \
  "${SUPABASE_URL}/rest/v1/camera_commands?status=eq.pending&select=id,camera" \
  -H "apikey: ${SUPABASE_SERVICE_KEY}" \
  -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}")
# Jos ei komentoja, lopeta
[ "$COMMANDS" = "[]" ] && exit 0
# Käy komennot läpi: nappaa JPEG, upload Storage, merkitse done/error
# (toteutus vaiheessa)
```

**`/etc/susieq.env`** (chmod 600):
```sh
SUPABASE_URL=https://<project>.supabase.co
SUPABASE_SERVICE_KEY=eyJ...
MASTO1_IP=<GL-XE300 LAN -osoite>
MASTO2_IP=<GL-XE300 LAN -osoite>
```

**Crontab (`/etc/crontabs/root`):**
```
* * * * * /usr/bin/susieq-sensors.sh
* * * * * /usr/bin/susieq-camera.sh
* * * * * sleep 10; /usr/bin/susieq-camera.sh
* * * * * sleep 20; /usr/bin/susieq-camera.sh
* * * * * sleep 30; /usr/bin/susieq-camera.sh
* * * * * sleep 40; /usr/bin/susieq-camera.sh
* * * * * sleep 50; /usr/bin/susieq-camera.sh
```

### 3. Supabase

**Taulut:**

```sql
-- Sensorilukema (säilytetään 7 päivää)
CREATE TABLE sensor_readings (
  id          bigserial PRIMARY KEY,
  recorded_at timestamptz DEFAULT now(),
  data        jsonb NOT NULL
);

-- Automaattinen siivous pg_cron:lla
SELECT cron.schedule('cleanup-readings', '0 3 * * *',
  'DELETE FROM sensor_readings WHERE recorded_at < now() - interval ''7 days''');

-- Kamerakomennot
CREATE TABLE camera_commands (
  id          bigserial PRIMARY KEY,
  created_at  timestamptz DEFAULT now(),
  camera      text NOT NULL CHECK (camera IN ('masto1', 'masto2')),
  status      text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'done', 'error'))
);
```

**Storage-bucket: `kamerat`**
- `masto1_latest.jpg` — ylikirjoitetaan joka snapshottipyynnöllä
- `masto2_latest.jpg` — sama

**Row Level Security:**
- `sensor_readings`: authenticated voi lukea, service_role kirjoittaa
- `camera_commands`: authenticated voi lukea ja lisätä, service_role päivittää statusta
- `kamerat`-bucket: authenticated voi lukea, service_role kirjoittaa

### 4. Mastokamerat (ESP32-CAM, 2 kpl)

- Toimivat **STA-modessa** (ei omaa AP:ta)
- Yhdistävät `SusieQ-Net`-verkkoon (GL-XE300:n AP)
- Tarjoavat JPEG-snapshotin esim. `/snapshot`-endpointista
- Suunnitellaan alusta alkaen tähän arkkitehtuuriin (bow-kamera pysyy ennallaan)

### 5. Netlify — frontend

**Hakemistorakenne:**
```
susieq-remote/
├── index.html
├── app.js
└── netlify.toml
```

**UI-osiot:**
- **Sensorikortit:** akku, solar, tuuli, tankkit (vesi/polttoaine/rommi), GPS-kartta, sää — sama dark-teema kuin paikallinen dashboard. Auto-refresh 60s.
- **Yhteysindikaattori:** vihreä (<2 min), keltainen (2–10 min), punainen (>10 min) — perustuu `recorded_at`-aikaleimaan
- **Mastokamerat:** "Masto 1 — Ota kuva" / "Masto 2 — Ota kuva" -painikkeet. Klikatessa: kirjoittaa `camera_commands`-tauluun → spinner → pollaa statusta → näyttää kuvan kun `done`

**Autentikointi:**
- Supabase Auth (sähköposti + salasana)
- Netlify-sivulla `anon`-avain — RLS estää kirjautumattoman pääsyn
- GL-XE300:lla `service_role`-avain — ei koskaan Netlify-koodissa

## Datavirrat

**Sensoridata (automaattinen):**
`ESP32 /data` → GL-XE300 curl 60s → `sensor_readings` → Netlify JS-client 60s → UI

**Kamerakuva (on-demand):**
Netlify "Ota kuva" -nappi → `camera_commands` pending → GL-XE300 camera.sh 10s → ESP32-CAM /snapshot → Supabase Storage → Netlify pollaa → näyttää kuvan

## Vaiheistus

1. Supabase-projekti: taulut, RLS, Storage-bucket
2. GL-XE300: verkkoasetukset (AP+STA), skriptit, crontab
3. Netlify: perus-frontend sensoridatalla
4. Mastokamerat: firmware (STA-mode, /snapshot-endpoint)
5. Kameratoiminnallisuus: camera_commands-flow GL-XE300 + Netlify
6. UI-hienosäätö ja visuaalisuus

## Rajaukset

- Bow-kamera (`SusieQ-Bow`) pysyy ennallaan, ei osaa tähän järjestelmään
- Ei live-videota — snapshots on-demand
- Ei historiakuvaajia tässä vaiheessa (vain viimeisin lukema näkyy)

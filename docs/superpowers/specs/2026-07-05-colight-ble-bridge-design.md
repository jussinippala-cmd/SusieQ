# Design: CoLight BLE-silta (susieq-remoten ohjauspaneelin taustatoteutus)

**Päivämäärä:** 2026-07-05
**Status:** Hyväksytty

---

## Tausta ja ongelma

susieq-remote-dashboardissa on jo valmis "Ohjauspaneeli"-välilehti CoLight 12/8 Gang
Switch Panelin etäohjaukseen (ks. `susieq-remote/docs/superpowers/specs/2026-06-21-ohjauspaneeli-design.md`,
status "Hyväksytty", skeema `switch_state`/`switch_commands` jo ajettu Supabaseen).
Se suunnitelma jätti tarkoituksella kaksi asiaa auki: BLE-kirjoitusprotokollan
selvittämisen ja bridge-laitteen (veneellä olevan sillan Supabasen ja paneelin välillä)
toteutuksen.

**Kirjoitusprotokolla on nyt ratkaistu ja live-vahvistettu** (`tools/colight-ble/FINDINGS.md`,
päivitetty 2026-07-05): täysi 12 kanavan komentotaulukko, ja vahvistettu että kirjoitus
on koko 6-tavuisen tilan snapshot — ei yksittäisen kanavan delta.

**Tämän suunnitelman scope on vain bridge:** ESP32-firmwaren uusi CoLight-moduuli +
GL-XE300:n uusi relay-skripti, jotka yhdessä toteuttavat jo olemassa olevan
`switch_state`/`switch_commands`-skeeman taakse. Ei kosketa susieq-remoten UI:ta eikä
Supabase-skeemaa — ne ovat jo valmiit.

**Rajaus:** vain etädashboard (susieq.net). Paikallisen cockpit-dashboardin suora
WebSocket-ohjaus on tarkoituksella jätetty myöhemmäksi, omaksi projektikseen.

## Päätökset (käyttäjän vahvistamat)

- **BLE-bridge-laite:** cockpit ESP32 (192.168.8.100) — tukee jo NimBLE:tä (Victron),
  on jo yhdistetty WiFi:hin. Riski (lisäkuorma jo kertaalleen jumiutuneelle laitteelle,
  ks. `project_esp32_power_outage`-muisti) hyväksytty tietoisesti.
- **BLE-yhteysstrategia:** yhdistä vain tarvittaessa (komennon tai taustapäivityksen
  yhteydessä), ei pidetä jatkuvaa yhteyttä auki. Minimoi taustakuorma WiFi+Modbus+HX711:n
  päälle.
- **GL-XE300 pollaustapa:** uusi jatkuva daemon-skripti (ei croni), 2 s silmukka —
  riittävän nopea 15 s asiakaspuolen timeoutille dashboardissa.
- **Kaikki 12 kanavaa mukaan** etäohjaukseen, myös LOWER/RAISE (moottoroitu
  winssi/mekanismi) — ei rajoitettu pelkkiin valoihin/laitteisiin.
- **Taustapäivitys tarpeen:** dashboard lukitsee kaikki napit jos `switch_state` on yli
  5 min vanha (`SWITCH_STALE_MS`, `susieq-remote/app.js`). Bridge päivittää tilan
  n. 2 min välein myös ilman aktiivisia komentoja, jotta paneeli pysyy käytettävissä.

## Arkkitehtuuri

```
Selain (susieq.net)
   │ INSERT switch_commands (channel, action)         [jo toteutettu]
   ▼
Supabase: switch_commands + switch_state              [jo olemassa, RLS+Realtime valmiina]
   │ GL-XE300 pollaa switch_commands 2 s välein         ← UUSI
   ▼
GL-XE300: susieq-colight.sh (jatkuva daemon)            ← UUSI
   │ HTTP POST http://192.168.8.100/colight {channel,action}
   │  TAI (taustapäivitys) GET http://192.168.8.100/colight/state
   ▼
Cockpit ESP32 (192.168.8.100): colight.h/colight.cpp    ← UUSI
   │ 1. Skannaa+yhdistä BLE:llä paneeliin ("MG-P1C-12P")
   │ 2. Lukee nykyisen 6-tavuisen tilakehyksen (beffa, f9-notifikaatio)
   │ 3. (komennolle) rakentaa uuden kehyksen: asettaa/poistaa pyydetyn
   │    kanavan bitin, säilyttää KAIKKI muut bitit ennallaan
   │ 4. (komennolle) kirjoittaa f5-kehyksen; (taustapäivitykselle) vain lukee
   │ 5. Katkaisee yhteyden, palauttaa 12 kanavan boolean-taulukon
   ▼
HTTP-vastaus GL-XE300:lle → PATCH switch_commands.status + INSERT switch_state
   ▼
Selain: Realtime-tilaus näkee muutokset heti                          [jo toteutettu]
```

## GL-XE300: `susieq-colight.sh`

Uusi init.d-palvelu (jatkuva prosessi, ei croni — 2 s pollausväli ei sovi croniin).

Silmukka (yksinkertaistettu):

```sh
#!/bin/sh
. /etc/susieq.env
LAST_REFRESH=0
REFRESH_INTERVAL=120  # sekuntia

while true; do
  NOW=$(date +%s)

  # 1) Käsittele yksi pending-komento kerrallaan (FIFO)
  ROW=$(curl -sf -H "apikey: $SUPABASE_KEY" -H "Authorization: Bearer $SUPABASE_KEY" \
    "${SUPABASE_URL}/rest/v1/switch_commands?status=eq.pending&select=id,channel,action&order=created_at.asc&limit=1")

  if [ -n "$ROW" ] && [ "$ROW" != "[]" ]; then
    ID=$(echo "$ROW" | jsonfilter -e '@[0].id')
    CHANNEL=$(echo "$ROW" | jsonfilter -e '@[0].channel')
    ACTION=$(echo "$ROW" | jsonfilter -e '@[0].action')

    curl -s -X PATCH -H "apikey: $SUPABASE_KEY" -H "Authorization: Bearer $SUPABASE_KEY" \
      -H "Content-Type: application/json" -d '{"status":"processing"}' \
      "${SUPABASE_URL}/rest/v1/switch_commands?id=eq.${ID}" >/dev/null

    RESULT=$(curl -sf -m 8 -X POST -H "Content-Type: application/json" \
      -d "{\"channel\":${CHANNEL},\"action\":\"${ACTION}\"}" \
      "http://192.168.8.100/colight")

    if [ $? -eq 0 ] && [ -n "$RESULT" ]; then
      CHANNELS=$(echo "$RESULT" | jsonfilter -e '@.state')
      curl -s -X POST -H "apikey: $SUPABASE_KEY" -H "Authorization: Bearer $SUPABASE_KEY" \
        -H "Content-Type: application/json" \
        -d "{\"data\":{\"valid\":true,\"channels\":${CHANNELS}}}" \
        "${SUPABASE_URL}/rest/v1/switch_state" >/dev/null
      curl -s -X PATCH ... -d '{"status":"done"}' "...switch_commands?id=eq.${ID}" >/dev/null
    else
      curl -s -X PATCH ... -d '{"status":"error"}' "...switch_commands?id=eq.${ID}" >/dev/null
    fi
    LAST_REFRESH=$NOW

  # 2) Ei pending-komentoa: taustapäivitys jos edellisestä yli REFRESH_INTERVAL
  elif [ $((NOW - LAST_REFRESH)) -ge $REFRESH_INTERVAL ]; then
    RESULT=$(curl -sf -m 8 "http://192.168.8.100/colight/state")
    if [ $? -eq 0 ] && [ -n "$RESULT" ]; then
      CHANNELS=$(echo "$RESULT" | jsonfilter -e '@.state')
      curl -s -X POST ... -d "{\"data\":{\"valid\":true,\"channels\":${CHANNELS}}}" \
        "${SUPABASE_URL}/rest/v1/switch_state" >/dev/null
    fi
    LAST_REFRESH=$NOW
  fi

  sleep 2
done
```

Yksi rivi kerrallaan (`limit=1`, `order=created_at.asc`) estää kaksi samanaikaista
BLE-operaatiota kilpailemasta ESP32:n yhteydestä. Taustapäivitys ja komentokäsittely
jakavat saman `LAST_REFRESH`-ajastimen, koska molemmat päivittävät tilan yhtä lailla.

## Cockpit ESP32: `colight.h`/`colight.cpp`

Noudattaa muiden sensorimoduulien rakennetta (vrt. `wind.h/cpp`, `tanks.h/cpp`), mutta
CoLight vaatii — toisin kuin Victronin passiivinen BLE-skannaus — oikean NimBLE
**GATT-clientin** (yhteys, ei vain mainos-datan kuuntelu).

**Kanavataulukko ja kehysmuoto:** `tools/colight-ble/FINDINGS.md`:n mukainen
(characteristic `0003cbbb-0000-1000-8000-00805f9beffa`, kehys `f5 G1 G2 G3 G4 G5 G6`,
G1 lepoarvo `0x22`, kanava PÄÄLLE = `+0x10`/`+0x01` oikeaan G-tavuun riippuen
parittomuudesta/parillisuudesta).

**`colight_read_state()`** — yhteinen apufunktio komennolle ja taustapäivitykselle:
1. Skannaa laite nimellä `MG-P1C-12P` (ei kiinteä MAC-osoite)
2. Yhdistä, tilaa `beffa`-notifikaatiot, odota `f9`-kehys (max 3 s)
3. Pura 6 tavua (G1-G6) 12 kanavan boolean-taulukoksi samalla nibble-kaavalla kuin
   kirjoituksessa. **Oletus, joka pitää vahvistaa toteutuksen yhteydessä:** notify-kehys
   raportoi pysyvän absoluuttisen tilan samalla G1-G6-koodauksella kuin kirjoitus (ei
   aiemmin oletettua "hetkellistä pulssia joka palautuu perustilaan") — tämä johdettiin
   siitä, että kirjoitus on koko tilan snapshot eikä delta, mutta ei ole erikseen
   live-testattu kahden kanavan pitkäaikaisella samanaikaisella päällä-tilalla.
4. Kanava 9 (RGB, ei kytketty) palautetaan aina `false`/`null` riippumatta datasta —
   dashboard piilottaa/disabloi sen joka tapauksessa, mutta bridge ei saa raportoida
   siitä sattumanvaraista arvoa.

**`colight_send_command(channel, action)`:**
1. Kutsuu `colight_read_state()` saadakseen nykyisen G1-G6-taulukon
2. Laskee uuden G-taulukon: asettaa/poistaa pyydetyn kanavan nibblen, säilyttää loput
3. Kirjoittaa `f5`-kehyksen `beffa`-characteristicille (write-without-response),
   odottaa lyhyen ack-notifikaation (max 1 s, ei kriittinen jos ei tule)
4. Katkaisee yhteyden, palauttaa uuden 12-kanavan tilan

**Koko operaatiolle (luku TAI komento) kova 6-8 s aikakatkaisu** — jos BLE jumiutuu,
palauta HTTP-virhe äläkä jää roikkumaan. Relevanttia koska ESP32:lla on jo 20 s
hardware-watchdog aiemman jumiutumisen takia (`project_esp32_power_outage`-muisti).

**Uudet HTTP-endpointit** (`ESPAsyncWebServer`, sama palvelin kuin nykyinen `/data`):
- `POST /colight` `{"channel":10,"action":"on"}` → `{"success":true,"state":[bool×12]}`
  tai `{"success":false,"error":"..."}`
- `GET /colight/state` → sama vastausmuoto, vain luku (taustapäivitystä varten)

Ei erillistä jatkuvaa taustapollausta ESP32:n `loop()`-silmukassa — molemmat endpointit
ovat pyyntöohjattuja, GL-XE300 päättää ajoituksen (komento heti, taustapäivitys 2 min
välein).

## Virhetilanteet

| Tilanne | Käsittely |
|---|---|
| Paneeli ei kantamalla / skannaus aikakatkaistu | ESP32 palauttaa `{"success":false,"error":"scan_timeout"}` → GL-XE300 merkitsee komennon `error`, ei kirjoita `switch_state`-riviä (ei julkaista epäluotettavaa tilaa) |
| BLE-yhteys katkeaa kesken | Sama kuin yllä, `error:"connection_lost"` |
| f9-notifikaatiota ei saada 3 s sisällä | `error:"no_state_notification"` |
| GL-XE300 → ESP32 HTTP-pyyntö aikakatkaistu (8 s) | `switch_commands.status = error`, ei `switch_state`-julkaisua |
| Kaksi komentoa nopeasti peräkkäin | Käsitellään järjestyksessä (`limit=1`, FIFO) — toinen odottaa jonossa korkeintaan ~10 s (yhden BLE-operaation kesto) |

## Testaussuunnitelma

1. Yksikkötestit ESP32-puolen G1-G6 ↔ 12-kanavan-taulukko-muunnoksille (puhdas funktio,
   ei BLE-riippuvuutta) — vrt. `tools/colight-ble/test_colight_ble.py`:n tyyli.
2. Manuaalinen testi veneellä: aja `/colight`-komento suoraan `curl`illa ESP32:ta
   vasten, vahvista fyysinen vaikutus jokaiselle kanavalle (paitsi 9).
3. Manuaalinen testi: kaksi kanavaa päälle peräkkäin, vahvista että molemmat pysyvät
   päällä (toistaa jo tehdyn Mac-testin, mutta ESP32-toteutuksella).
4. GL-XE300-daemon: käynnistä palvelu, seuraa lokia komennon läpimenosta päästä päähän
   (selain → Supabase → GL-XE300 → ESP32 → paneeli → takaisin).
5. Taustapäivitys: odota >2 min ilman komentoja, vahvista uusi `switch_state`-rivi
   ilmestyy automaattisesti.
6. Staleness: sammuta ESP32 tai katkaise BLE keinotekoisesti, vahvista että dashboard
   lukitsee napit ~5 min kuluttua.

## Ei tähän suunnitelmaan kuuluvaa

- Paikallisen cockpit-dashboardin suora WebSocket-ohjaus (myöhempi, erillinen projekti).
- susieq-remoten UI tai Supabase-skeema (jo valmiit, ei muutoksia).
- Kanava 9:n (RGB) kytkeminen mihinkään fyysisesti — pysyy aina käyttämättömänä.

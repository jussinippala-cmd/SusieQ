# Cockpit-ESP32 watchdogin vahvistus — design

## Ongelma

Cockpit-ESP32 (192.168.8.100) tippui offline-tilaan aamulla 2026-07-06.
Nykyinen 20s hardware task watchdog (`WDT_TIMEOUT_S`, `main.cpp`, commit
`41ec506`) valvoo vain `loop()`-tehtävää: jos `loop()` jatkaa tikitystä
normaalisti, watchdog ei laukea, vaikka jokin muu osa firmwarea olisi jumissa.

Todennäköisin syy tämän aamun jumille: `colight.cpp`:n
`colight_task_fn`-tausta-taski (deployattu 2026-07-05, ks.
[2026-07-05-colight-persistent-ble-cache-design.md](2026-07-05-colight-persistent-ble-cache-design.md))
ottaa `colight_mutex`in neljässä kohdassa `xSemaphoreTake(colight_mutex,
portMAX_DELAY)` — ikuisella odotuksella. Jos tämä taski tai NimBLE:n oma
host-task jää jumiin mutex pidossa, `/colight` ja `/colight/state`
-HTTP-pyynnöt (ja todennäköisesti koko AsyncTCP-taski, koska
ESPAsyncWebServerin callback-suoritus on sarjallista samalla taskilla) jäävät
ikuisesti roikkumaan. `loop()` ei koskaan kosketa `colight_mutex`ia, joten se
jatkaa tikitystä ja syöttää 20s watchdogia normaalisti koko ajan — watchdog ei
siis huomaisi tätä jumia, vaikka timeout-arvoa nostettaisiin.

Käyttäjän alkuperäinen pyyntö oli nostaa watchdog-timeout ~70s:iin, jotta se
ei keskeytä OTA-siirtoja. OTA-syöttö (`ArduinoOTA.onProgress`, commit
`b6ea679`) on jo korjattu ja deployattu — timeoutin nosto on siis lisämarginaali
hitaalle/heikolle WiFi-siirrolle, ei korjaus jo ratkaistuun OTA-ongelmaan.

## Ratkaisu

Kolme toisiaan täydentävää muutosta:

### 1. Hardware watchdog: 20s → 70s

`susieq_dashboard/src/main.cpp`: `#define WDT_TIMEOUT_S 20` → `70`. Ei muita
koodimuutoksia — olemassa oleva `onProgress`-syöttö riittää edelleen.

### 2. HTTP-vasteaika-watchdog (uusi)

Sovellustason varmistus, joka kattaa hangit `loop()`-tehtävän ulkopuolella
(esim. yllä kuvattu `colight_mutex`-tyyppinen deadlock, tai mikä tahansa muu
tuntematon jumi joka jättää `loop()`:n elossa mutta HTTP-serverin
vastaamattomaksi):

- Uusi globaali `last_data_request_ms`, päivittyy jokaisella `/data`-pyynnöllä.
  `/data`-endpointia pollaa `susieq-sensors.sh` kerran minuutissa modeemilta
  riippumatta käyttäjän selaamisesta — luonnollinen "elossa"-signaali, ei vaadi
  muutoksia muihin HTTP-handlereihin.
- Uusi globaali `ota_active`, `true` `ArduinoOTA.onStart`:ssa, `false`
  `onEnd`/`onError`:ssa. Watchdog ei koskaan laukea kesken OTA-siirron.
- `loop()`:ssa: jos `WiFi.status() == WL_CONNECTED && !ota_active &&
  millis() - last_data_request_ms > 180000` → lokiviesti + `esp_restart()`.
- `last_data_request_ms` alustetaan `millis()`:iin `setup()`:n lopussa (ei
  laukea heti bootin jälkeen ennen ensimmäistä pyyntöä).
- Timeout 180s valittu koska: (a) selvästi pidempi kuin normaali OTA-siirto,
  (b) selvästi pidempi kuin yksittäinen ohitettu 60s cron-pollaus, (c) silti
  riittävän lyhyt palauttamaan laitteen automaattisesti ilman fyysistä
  virtakiertoa merkittävän odotusajan sisällä.
- WiFi-ehto (`WL_CONNECTED`) varmistaa ettei watchdog laukea silloin kun
  todellinen syy on WiFi/4G-yhteyden katko (sitä hoitaa jo olemassa oleva
  `WiFi.reconnect()`-logiikka `loop()`:ssa) — vain tilanteessa jossa WiFi on
  yhä assosioituna mutta HTTP ei vastaa.

### 3. `colight_mutex`-odotusten rajaus (juurisyykorjaus)

`susieq_dashboard/src/colight.cpp`: kaikki neljä
`xSemaphoreTake(colight_mutex, portMAX_DELAY)`-kutsua muutetaan
`xSemaphoreTake(colight_mutex, pdMS_TO_TICKS(2000))`:ksi (sama 2s-käytäntö
kuin osassa `hx711_mutex`-kutsuja muualla koodissa):

- `colight_notify_handler` (NimBLE-host-taski): epäonnistuessa pudotetaan tämä
  frame — uusi notifikaatio tulee pian uudestaan, ei toiminnallista haittaa.
- `ColightClientCB::onDisconnect`: epäonnistuessa lokitetaan varoitus ja
  palataan — harvinainen polku, ei vaadi erikoiskäsittelyä.
- `colight_task_fn` (2 kohtaa: connected-lipun luku, uuden
  client/chr-julkaisu): epäonnistuessa lokitetaan varoitus ja yritetään
  uudelleen seuraavalla for(;;)-kierroksella sen sijaan että jäädään jumiin.
- `colight_read_state()` / `colight_send_command()`: epäonnistuessa
  palautetaan `ColightResult` jossa `error = "busy"`, `success = false` —
  sama kuvio kuin `hx711_mutex`in 503 "Sensor busy" -vastauksissa. HTTP-pyyntö
  saa nopean, selkeän vastauksen roikkumisen sijaan.

Tämä poistaa deadlock-mahdollisuuden `colight_mutex`in osalta käytännössä
kokonaan. Kohta 2 toimii varmistuksena mille tahansa muulle
ennalta-arvaamattomalle hangille (esim. NimBLE-pino itse jumissa jossain
kutsussa jota emme suoraan kontrolloi).

## Ei kuulu tähän speciin

- AsyncWebServer/NimBLE-kirjastojen sisäisten taskien rekisteröinti
  `esp_task_wdt`:hen suoraan — API tähän ei ole vakaasti saatavilla kummastakaan
  kirjastosta, ja HTTP-vasteaika-watchdog (kohta 2) kattaa saman
  hangin havaitsemisen sovellustasolla ilman kirjastojen sisäisiin
  toteutuksiin kajoamista.
- `client->connect()`/`getService()`/`subscribe()`-kutsujen omat sisäiset
  timeoutit NimBLE-pinossa — `setConnectTimeout(5)` on jo asetettu
  connect()-kutsulle; muiden kutsujen oletusaikakatkaisut jätetään ennalleen,
  koska kohta 2 kattaa senkin varalta ettei niiden oma timeout riittäisi.

## Testaus

- `pio run -e susieq-ota -t upload` cockpitille (192.168.8.100) SusieQ-Net-
  verkosta.
- Flashin jälkeen: `/data` ja `/colight/state` vastaavat normaalisti,
  `uptime_s` kasvaa odotetusti, `[wdt] watchdog käynnissä, timeout 70 s`
  näkyy sarjamonitorissa jos saatavilla.
- Deadlock-skenaariota (kohdat 2 ja 3) ei voi turvallisesti simuloida veneellä
  live-BLE-paneelin kanssa — varmistetaan koodikatselmoinnilla ja
  normaalitoiminnan regressiotarkistuksella (kytkinpaneelin fyysinen
  painallus, nettiohjaus `/colight`, tila päivittyy `/colight/state`:en kuten
  ennenkin).

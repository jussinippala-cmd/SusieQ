# OTA watchdog bootstrap — design

## Ongelma

Cockpit-ESP32:n (192.168.8.100) nykyinen asennettu dashboard-firmware resetoi
itsensä ~20s kuluttua hardware watchdogin (`WDT_TIMEOUT_S = 20`,
`susieq_dashboard/src/main.cpp`) toimesta kesken OTA-siirron. Syy: `loop()`
syöttää watchdogia joka kierroksella, mutta `ArduinoOTA.handle()` käsittelee
koko siirron yhdessä blokkaavassa kutsussa eikä palaa `loop()`:iin ennen kuin
siirto on valmis. Korjaus (watchdogin syöttö `ArduinoOTA.onProgress`
-callbackissa) on jo commitoitu mainiin (`b6ea679`), mutta sitä ei ole saatu
laitteelle asti, koska korjauksen vieminen vaatii juuri sen OTA-siirron joka
epäonnistuu ilman korjausta — muna-kana-ongelma. Fyysistä USB-pääsyä laitteeseen
ei tällä hetkellä ole.

## Ratkaisu

Kaksivaiheinen OTA:

1. **Bootstrap-firmware** (`susieq_dashboard/tools/ota_bootstrap/`) — pelkkä
   WiFi STA + ArduinoOTA, ei sensoreita, ei web-serveriä, ei BLE:tä, ei
   watchdogia. Mitattu koko: 763 077 tavua vs. täyden firmwaren 1 185 477
   tavua (~64 %). Pienempi koko antaa siirrolle hyvät mahdollisuudet mahtua
   alle 20s ikkunaan ennen kuin nykyinen (vanha) firmware resetoi.
2. Kun bootstrap pyörii (ei watchdogia → ei aikapainetta), viedään sen päälle
   nykyinen täysi dashboard-firmware (sisältää jo watchdog-korjauksen ja
   BLE-kytkinpaneeliohjauksen).

## Turvallisuus

ESP32:n OTA-mekanismi merkitsee uuden partition käynnistettäväksi vasta koko
siirron onnistuneen validoinnin jälkeen. Jos vaihe epäonnistuu kesken (esim.
watchdog resetoi), laite bootaa automaattisesti takaisin edelliseen toimivaan
firmwareen. Yritykset ovat siis turvallisesti toistettavissa ilman
bricking-riskiä.

## Tunnettu rajoitus

Vaiheen 1 aikana (bootstrap pyörii) dashboard, sensorit ja
BLE-kytkinpaneeliohjaus ovat pois päältä, kunnes vaihe 2 onnistuu.

## Käytetyt tunnukset (samat kuin tuotanto-OTA:ssa)

- WiFi: `SusieQ-Net` / ks. `config.h`
- OTA-hostname: `susieq-cockpit`, salasana: ks. `config.h`
- Kohde: `192.168.8.100`

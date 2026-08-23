#pragma once
#include <cstdint>

// Puhdas päätöslogiikka POST /sleep -pyynnölle. Ei Arduino-/ESP-riippuvuuksia,
// jotta tämä voidaan testata natiivisti — ks. test/test_sleep_policy/ ja
// docs/superpowers/specs/2026-08-23-esp32-yolepotila-design.md.

enum SleepVerdict {
    SLEEP_OK = 0,
    SLEEP_BOOT_GRACE,     // laite buutannut liian äskettäin
    SLEEP_OTA_ACTIVE,     // OTA-siirto kesken
    SLEEP_BAD_DURATION,   // kesto rajojen ulkopuolella
};

// Laite on herättyään vähintään tämän ajan hereillä ennen kuin unikomennot
// hyväksytään. Tämä on suunnitelman tärkein turvamekanismi: se takaa OTA-ikkunan
// joka bootin jälkeen. Ilman sitä unipolun bugi voisi lukita laitteen tilaan,
// josta ainoa ulospääsy on fyysinen käynti veneellä — etäpalautusta ei ole.
constexpr uint32_t SLEEP_BOOT_GRACE_MS = 600000;   // 10 min

// Kova katto: mikään syöte ei voi nukuttaa laitetta tätä pidempään.
constexpr uint32_t SLEEP_MIN_S = 60;
constexpr uint32_t SLEEP_MAX_S = 28800;            // 8 h

// Tarkistusjärjestys on merkitsevä: boot_grace → ota_active → kesto. Näin
// vasta buutannut laite vastaa aina "boot_grace" riippumatta siitä, onko
// pyynnön kesto järkevä — diagnoosi kertoo tärkeimmän esteen ensin.
SleepVerdict sleep_policy_check(uint32_t uptime_ms, bool ota_active, uint32_t seconds);

// Virhetunniste JSON-vastaukseen. Palauttaa "" kun verdict on SLEEP_OK.
const char* sleep_verdict_error(SleepVerdict v);

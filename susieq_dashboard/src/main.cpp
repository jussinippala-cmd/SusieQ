#include <Arduino.h>
#include <cmath>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

#include "../include/config.h"
#include "wind.h"
#include "tanks.h"
#include "fuel.h"
#include "victron.h"
#include "gps.h"
#include "weather.h"
#include "rum.h"
#include "colight.h"
#include "sleep_policy.h"

// ─── Globals ──────────────────────────────────────────────────────────
AsyncWebServer  server(80);

// loop() jumiutuu -> WDT ei saa reset-kutsua -> ESP buuttaa itsensä
#define WDT_TIMEOUT_S 70

// Sovellustason varmistus loop()-tehtävän ULKOPUOLISILLE hangeille (esim.
// AsyncTCP/colight_mutex-tyyppinen deadlock, jota edellä oleva hardware WDT
// ei näe koska loop() jatkaa tikitystä normaalisti). Jos /data ei ole
// vastannut tähän aikaan WiFi:n ollessa yhä yhdistettynä eikä OTA käynnissä,
// laite bootataan itse.
//
// Reboot-silmukkasuoja: koska tämä nojaa ULKOISEEN /data-pollaukseen
// (susieq-sensors.sh cron modeemilla), pollauksen pysähtyminen WiFi:n
// pysyessä ylhäällä bootittaisi tervettä laitetta 180 s välein loputtomasti.
// RTC-muistissa säilyvä laskuri sallii enintään LIVENESS_RESTART_LIMIT
// peräkkäistä liveness-boottia; sen jälkeen laite jää käyntiin (hardware WDT
// suojaa yhä loop()-jumit) ja laskuri nollautuu vasta kun /data-pyyntö taas
// saapuu tai virrat katkaistaan.
#define HTTP_LIVENESS_TIMEOUT_MS 180000
#define LIVENESS_RESTART_LIMIT   3
RTC_NOINIT_ATTR static uint32_t liveness_restart_count;
static volatile uint32_t last_data_request_ms = 0;
static volatile bool ota_active = false;

// ── Yölepotila ────────────────────────────────────────────────────────
// Modeemin susieq-sleep.sh kutsuu POST /sleep -endpointia klo 22 kaikkien
// omien porttiensa (away / no_sleep / Supabase sleep_enabled) läpäisyn ja
// onnistuneen AT+CFUN=4:n jälkeen. ESP32 ei tiedä kellonaikaa eikä satamatilaa,
// joten kesto tulee pyynnön mukana.
//
// Uni EI tapahdu HTTP-käsittelijässä: se ajetaan AsyncTCP-taskissa, joka ei
// ehtisi lähettää vastausta ennen kuin laite katoaa. Käsittelijä asettaa nämä
// ja loop() suorittaa unen 2 s myöhemmin.
static volatile uint32_t pending_sleep_s = 0;
static volatile uint32_t sleep_at_ms     = 0;

// Boottidiagnostiikka /data-JSON:iin: reset-syy + boottilaskuri, jotta
// odottamattoman bootin syy (task_wdt/panic/brownout/sw) selviää etänä
// ilman USB-sarjaporttia. Laskuri elää RTC-muistissa lämpimien boottien
// yli; magic-sentinel erottaa kylmäkäynnistyksen satunnaisesta RTC-datasta.
#define BOOT_COUNT_MAGIC 0x5B007C47
RTC_NOINIT_ATTR static uint32_t boot_count;
RTC_NOINIT_ATTR static uint32_t boot_count_magic;

// Kuka kutsui esp_restart()? Shutdown-handler ajetaan vain esp_restart-
// polulla (ei panicissa/WDT:ssä) ja tallentaa kutsuvan taskin nimen RTC-
// muistiin. Omat tunnetut restart-paikat (liveness, OTA) kirjoittavat
// tarkemman selitteen ennen restartia, jolloin handler ei ylikirjoita.
// Tyhjä note sw-bootin jälkeen = restart tuli ohi esp_restart():in
// (esim. tupla-exception, jonka hint-mappaus näyttää sw:ltä).
#define RESTART_NOTE_MAGIC 0xC0FFEE01
RTC_NOINIT_ATTR static char restart_note[120];
RTC_NOINIT_ATTR static uint32_t restart_note_magic;
static char last_restart_note[120] = "";

static void note_restart(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(restart_note, sizeof(restart_note), fmt, args);
    va_end(args);
    restart_note_magic = RESTART_NOTE_MAGIC;
}

static void restart_shutdown_handler(void) {
    if (restart_note_magic != RESTART_NOTE_MAGIC) {
        note_restart("esp_restart task=%s uptime=%lus",
                     pcTaskGetName(NULL), (unsigned long)(millis() / 1000));
    }
}

static const char* reset_reason_str() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "poweron";
        case ESP_RST_SW:        return "sw";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

static unsigned long last_sensor_read = 0;
static String cached_json;             // cached for /data
static SemaphoreHandle_t hx711_mutex = nullptr;  // protects HX711 access

// ─── JSON builder ─────────────────────────────────────────────────────
static String build_json() {
    JsonDocument doc;
    WindData    wind    = wind_read();
    TankData    water   = tanks_read();
    FuelData    fuel    = fuel_read();
    VictronData victron = victron_get();

    doc.clear();

    // Wind
    doc["wind"]["valid"] = wind.valid;
    if (wind.valid) {
        doc["wind"]["speed"]     = round(wind.speed_ms * 10) / 10.0;
        doc["wind"]["direction"] = round(wind.direction);
    }

    // Battery (Victron BLE only)
    doc["battery"]["valid"] = victron.valid;
    if (victron.valid) {
        doc["battery"]["voltage"] = round(victron.battery_voltage * 100) / 100.0;
        doc["battery"]["current"] = round(victron.battery_current * 100) / 100.0;
        doc["battery"]["soc"]     = round(battery_voltage_to_soc(victron.battery_voltage));
    }

    // Solar (Victron only)
    doc["solar"]["valid"] = victron.valid;
    if (victron.valid) {
        doc["solar"]["pv_power"]       = victron.pv_power_w;
        doc["solar"]["pv_voltage"]     = victron.pv_voltage;
        doc["solar"]["yield_today_wh"] = victron.yield_today_wh;
        doc["solar"]["state"]          = victron.charge_state;
    }

    // Water tank
    doc["water"]["valid"] = water.valid;
    if (water.valid) {
        doc["water"]["liters"]    = round(water.liters * 10) / 10.0;
        doc["water"]["level_pct"] = water.level_pct;
    }

    // Fuel
    doc["fuel"]["valid"] = fuel.valid;
    if (fuel.valid) {
        doc["fuel"]["liters"] = round(fuel.liters * 10) / 10.0;
        doc["fuel"]["pct"]    = round(fuel.pct);
    }

    // Rum
    RumData rum = rum_read();
    doc["rum"]["valid"] = rum.valid;
    if (rum.valid) {
        doc["rum"]["liters"] = round(rum.liters * 100) / 100.0;
        doc["rum"]["pct"]    = round(rum.pct);
    }

    // GPS
    GpsData gps = gps_read();
    doc["gps"]["fix"]   = gps.fix;
    doc["gps"]["valid"] = gps.valid;
    if (gps.fix) {
        doc["gps"]["sog_knots"] = round(gps.sog_knots * 10) / 10.0;
        doc["gps"]["cog_deg"]   = (int)round(gps.cog_deg);
        doc["gps"]["lat"]       = gps.lat;
        doc["gps"]["lon"]       = gps.lon;
    }
    if (gps.time_valid) {
        doc["gps"]["utc_h"] = gps.hour;
        doc["gps"]["utc_m"] = gps.minute;
        doc["gps"]["utc_s"] = gps.second;
    }

    // Weather (AHT20 + BMP280 + DS18B20)
    WeatherData weather = weather_read();
    doc["weather"]["valid"] = weather.valid;
    if (weather.aht_valid) {
        doc["weather"]["air_temp"]   = round(weather.air_temp   * 10) / 10.0;
        doc["weather"]["humidity"]   = round(weather.humidity);
    }
    if (weather.bmp_valid) {
        doc["weather"]["pressure"]   = round(weather.pressure   * 10) / 10.0;
    }
    if (weather.ds_valid) {
        doc["weather"]["water_temp"] = round(weather.water_temp * 10) / 10.0;
    }

    // System uptime (64-bit to avoid 49-day millis() overflow)
    doc["uptime_s"] = (unsigned long)(esp_timer_get_time() / 1000000ULL);
    doc["reset_reason"] = reset_reason_str();
    doc["boot_count"]   = boot_count;
    doc["restart_note"] = last_restart_note;

    String out;
    serializeJson(doc, out);
    return out;
}

static String colight_result_to_json(const ColightResult& r) {
    JsonDocument doc;
    doc["success"] = r.success;
    // "busy" tarkoittaa ettei välimuistia päästy lukemaan — silloin
    // connected/last_updated_ms olisivat structin oletusarvoja (false/0),
    // mikä näyttäisi kutsujalle identtiseltä "ei koskaan yhdistetty"-tilalta.
    // Jätetään kentät pois, jotta hetkellinen kilpailutilanne ei väitä
    // yhteyden olevan poikki.
    if (strcmp(r.error, "busy") != 0) {
        doc["connected"] = r.connected;
        doc["last_updated_ms"] = r.last_updated_ms;
        // Läsnä vain kun tila on RTC-palautettu eikä aitoa kehystä ole vielä
        // nähty — susieq.net voi halutessaan näyttää tämän; modeemin daemon
        // ei lue kenttää.
        if (r.restored) doc["restored"] = true;
        // Läsnä vain kun tila on virtakierron jälkeinen kaikki pois -oletus
        // eikä aitoa kehystä ole vielä nähty.
        if (r.assumed) doc["assumed"] = true;
    }
    if (r.success) {
        JsonArray arr = doc["state"].to<JsonArray>();
        for (int i = 0; i < COLIGHT_NUM_CHANNELS; i++) arr.add(r.channels[i]);
    } else {
        doc["error"] = r.error;
    }
    String out;
    serializeJson(doc, out);
    return out;
}

// ─── HTTP routes ──────────────────────────────────────────────────────
static void setup_routes() {
    // Dashboard siirretty modeemille — ks. susieq_glxe300/susieq-dashboard-server.py
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", "Dashboard: http://192.168.8.1:8081/");
    });

    // JSON snapshot endpoint (for debugging without WebSocket)
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest* req) {
        last_data_request_ms = millis();
        liveness_restart_count = 0;  // pollaus elää — nollaa reboot-silmukkasuoja
        // Take hx711_mutex to avoid racing loop()'s cached_json reassignment.
        String snapshot;
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(50))) {
            snapshot = cached_json;
            xSemaphoreGive(hx711_mutex);
        } else {
            snapshot = cached_json;
        }
        req->send(200, "application/json", snapshot.length() > 0 ? snapshot : "{}");
    });

    // Tare fuel scale (call from browser or curl)
    // Uses hx711_mutex to avoid concurrent HX711 access with loop()
    server.on("/tare", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
            fuel_tare();
            xSemaphoreGive(hx711_mutex);
            req->send(200, "text/plain", "Tare saved");
        } else {
            req->send(503, "text/plain", "Sensor busy");
        }
    });

    // Tare water scale
    server.on("/tare_water", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
            water_tare();
            xSemaphoreGive(hx711_mutex);
            req->send(200, "text/plain", "Water tare saved");
        } else {
            req->send(503, "text/plain", "Sensor busy");
        }
    });

    // Tare rum scale
    server.on("/tare_rum", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
            rum_tare();
            xSemaphoreGive(hx711_mutex);
            req->send(200, "text/plain", "Rum tare saved");
        } else {
            req->send(503, "text/plain", "Sensor busy");
        }
    });

    // Calibrate rum scale — POST /calibrate_rum?g=<grams>
    // e.g. curl -X POST "http://192.168.4.1/calibrate_rum?g=500"
    server.on("/calibrate_rum", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("g")) {
            req->send(400, "text/plain", "Missing ?g=<grams>");
            return;
        }
        float grams = req->getParam("g")->value().toFloat();
        if (grams <= 0.0f) {
            req->send(400, "text/plain", "Invalid weight");
            return;
        }
        float known_kg = grams / 1000.0f;
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(5000))) {
            bool ok = rum_calibrate(known_kg);
            xSemaphoreGive(hx711_mutex);
            if (ok) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Calibrated with %.0f g — OK", grams);
                req->send(200, "text/plain", buf);
            } else {
                req->send(500, "text/plain", "Calibration failed — scale not ready?");
            }
        } else {
            req->send(503, "text/plain", "Sensor busy");
        }
    });

    // Debug: raw rum reading in kg — GET /rum_raw
    server.on("/rum_raw", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
            float kg = rum_get_raw_units();
            xSemaphoreGive(hx711_mutex);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.4f kg", kg);
            req->send(200, "text/plain", buf);
        } else {
            req->send(503, "text/plain", "Sensor busy");
        }
    });

    // CORS-preflight /records:lle — selain lähettää Content-Type: application/json,
    // joka laukaisee OPTIONS-esipyynnön kun dashboard pyörii eri originissa (modeemi).
    server.on("/records", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        req->send(200);
    });

    // GET /records → lue /records.json LittleFS:stä
    server.on("/records", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!LittleFS.exists("/records.json")) {
            req->send(200, "application/json", "{}");
            return;
        }
        File f = LittleFS.open("/records.json", "r");
        String body = f.readString();
        f.close();
        req->send(200, "application/json", body);
    });

    // POST /records → kirjoita /records.json LittleFS:ään
    // index==0: avaa kirjoitukseen; index>0: lisää perään (multi-chunk body)
    server.on("/records", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            req->send(200, "text/plain", "OK");
        },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len,
           size_t index, size_t total) {
            File f = LittleFS.open("/records.json", index == 0 ? "w" : "a");
            if (f) { f.write(data, len); f.close(); }
        }
    );

    server.on("/victron-debug", HTTP_GET, [](AsyncWebServerRequest* req) {
        VictronDebug d = victron_debug_get();
        String hex = "";
        for (int i = 0; i < d.last_pkt_len; i++) {
            char h[3]; snprintf(h, sizeof(h), "%02X", d.last_pkt[i]);
            hex += h;
        }
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"ble_seen\":%lu,\"victron_seen\":%lu,\"decrypt_fail\":%lu,\"decrypt_ok\":%lu,\"last_mac\":\"%s\",\"pkt_len\":%u,\"pkt\":\"%s\"}",
            (unsigned long)d.ble_seen, (unsigned long)d.victron_seen,
            (unsigned long)d.decrypt_fail, (unsigned long)d.decrypt_ok,
            d.last_mac, d.last_pkt_len, hex.c_str());
        req->send(200, "application/json", buf);
    });

    // POST /colight?channel=<1..12>&action=on|off
    server.on("/colight", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("channel") || !req->hasParam("action")) {
            req->send(400, "application/json", "{\"success\":false,\"error\":\"missing_params\"}");
            return;
        }
        int channel = req->getParam("channel")->value().toInt();
        String action = req->getParam("action")->value();
        if (channel < 1 || channel > 12 || (action != "on" && action != "off")) {
            req->send(400, "application/json", "{\"success\":false,\"error\":\"invalid_params\"}");
            return;
        }
        ColightResult r = colight_send_command(channel, action == "on");
        req->send(200, "application/json", colight_result_to_json(r));
    });

    // GET /colight/state — read-only refresh, no panel side effects
    server.on("/colight/state", HTTP_GET, [](AsyncWebServerRequest* req) {
        ColightResult r = colight_read_state();
        req->send(200, "application/json", colight_result_to_json(r));
    });

    // GET /colight-debug — last connect/disconnect/scan events, plain text,
    // one per line. Diagnostic only, readable from any browser without USB.
    server.on("/colight-debug", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", colight_get_log());
    });

    // Yölepotila — ks. docs/superpowers/specs/2026-08-23-esp32-yolepotila-design.md
    server.on("/sleep", HTTP_POST, [](AsyncWebServerRequest* req) {
        // Modeemi lähettää POST /sleep?seconds=N ilman runkoa, joten parametri
        // luetaan query-stringistä (getParam:in oletus on post=false).
        uint32_t seconds = 0;
        if (req->hasParam("seconds")) {
            seconds = (uint32_t)strtoul(req->getParam("seconds")->value().c_str(), nullptr, 10);
        }

        SleepVerdict v = sleep_policy_check(millis(), ota_active, seconds);
        if (v != SLEEP_OK) {
            int code = (v == SLEEP_BAD_DURATION) ? 400 : 409;
            char body[80];
            snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}",
                     sleep_verdict_error(v));
            Serial.printf("[sleep] hylätty: %s (seconds=%lu)\n",
                          sleep_verdict_error(v), (unsigned long)seconds);
            req->send(code, "application/json", body);
            return;
        }

        pending_sleep_s = seconds;
        sleep_at_ms     = millis() + 2000;
        char body[64];
        snprintf(body, sizeof(body), "{\"ok\":true,\"seconds\":%lu}",
                 (unsigned long)seconds);
        Serial.printf("[sleep] hyväksytty: %lu s\n", (unsigned long)seconds);
        req->send(200, "application/json", body);
    });

    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });
}

// ─── WiFi STA ─────────────────────────────────────────────────────────
static void start_wifi_sta() {
    IPAddress ip, gw, sn;
    ip.fromString(STATIC_IP);
    gw.fromString(GATEWAY_IP);
    sn.fromString(SUBNET_MASK);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.config(ip, gw, sn);
    WiFi.begin(STA_SSID, STA_PASSWORD);

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        Serial.printf("[wifi] yhdistetty: %s  RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.println("[wifi] yhdistyminen epäonnistui — jatketaan silti");
    }
}

// ─── Setup ────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[SusieQ] starting up...");

    // RTC-noinit-muisti on satunnaista kylmäkäynnistyksen jälkeen — laskuri
    // on luotettava vain ohjelmallisen resetin (esp_restart) yli.
    if (esp_reset_reason() != ESP_RST_SW) {
        liveness_restart_count = 0;
    }
    if (boot_count_magic != BOOT_COUNT_MAGIC) {
        boot_count_magic = BOOT_COUNT_MAGIC;
        boot_count = 0;
    }
    boot_count++;
    if (restart_note_magic == RESTART_NOTE_MAGIC) {
        restart_note[sizeof(restart_note) - 1] = '\0';
        strncpy(last_restart_note, restart_note, sizeof(last_restart_note));
        restart_note_magic = 0;  // kulutettu — seuraava bootti aloittaa puhtaalta
    }
    esp_register_shutdown_handler(restart_shutdown_handler);
    Serial.printf("[boot] reset_reason=%s boot_count=%u note='%s'\n", reset_reason_str(),
                  (unsigned)boot_count, last_restart_note);

    // Deep sleepin jäljiltä MAX485:n DE-pinni on RTC-lukossa (ks. enter_deep_sleep).
    // Lukitus puretaan EHDOITTA joka bootilla, ei vain deep sleep -heräyksessä:
    // RTC-pinnin hold elää RTC-verkkotunnuksessa eikä nollaudu tavallisessa
    // resetissä, joten heräyksen ja tämän rivin välissä sattuva brownout tai
    // panic jättäisi pinnin pysyvään lukkoon — tuulilukema kuolisi hiljaa eikä
    // OTA tai esp_restart() palauttaisi sitä, vain päävirtakytkimen kierto.
    // Kutsu on turvallinen jokaisella bootilla: se vain nollaa hold-bitin.
    rtc_gpio_hold_dis((gpio_num_t)WIND_DE_PIN);
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
        Serial.println("[sleep] herätty deep sleepistä, DE-pinnin lukitus purettu");
    }

    // Filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("[fs] LittleFS mount failed!");
    } else {
        Serial.println("[fs] LittleFS mounted");
    }

    // HX711 mutex (protects concurrent tare vs read)
    hx711_mutex = xSemaphoreCreateMutex();

    // Sensors
    wind_init();
    tanks_init();
    fuel_init();
    victron_init();
    // Diagnostiikkatoggle: -DCOLIGHT_DISABLED_FOR_DIAGNOSIS build-lipulla saa
    // buildin, jossa CoLight-yhteyttä ei alusteta mutta Victron-BLE-skannaus
    // jää päälle (A/B-vikaeristys; endpointit palauttavat silloin "busy").
#if !defined(COLIGHT_DISABLED_FOR_DIAGNOSIS)
    colight_init();
#else
    Serial.println("[colight] POIS PÄÄLTÄ (diagnostiikkakoe)");
#endif
    gps_init();
    weather_init();    // AHT20 + BMP280 (I2C) + DS18B20 (1-Wire)
    rum_init();

    start_wifi_sta();

    // Hardware watchdog — buuttaa ESP:n automaattisesti jos loop() jumiutuu
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);
    Serial.printf("[wdt] watchdog käynnissä, timeout %d s\n", WDT_TIMEOUT_S);

    // OTA
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        ota_active = true;
        Serial.println("[ota] update starting...");
    });
    ArduinoOTA.onEnd([]() {
        ota_active = false;
        note_restart("ota done");
        Serial.println("\n[ota] update complete — rebooting");
    });
    ArduinoOTA.onError([](ota_error_t err) {
        ota_active = false;
        Serial.printf("[ota] error %u\n", err);
    });
    // Feed the hardware watchdog on every OTA chunk, not just once per loop()
    // iteration — a slow/weak WiFi link can make a single ArduinoOTA.handle()
    // call block long enough on its own to trip the 70s watchdog mid-transfer.
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        esp_task_wdt_reset();
    });
    ArduinoOTA.begin();
    Serial.printf("[ota] ready — hostname: %s\n", OTA_HOSTNAME);

    // CORS — dashboard pyörii nyt modeemilla (eri origin), kalibrointikutsut
    // (/tare, /tare_water, /tare_rum, /records) tulevat siis cross-origin.
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Web server (ei enää WebSocketia — /data riittää modeemin pollaukseen)
    setup_routes();
    server.begin();
    Serial.println("[http] server started on port 80");
    last_data_request_ms = millis();  // baseline for HTTP-liveness watchdog

    Serial.printf("[SusieQ] ready — open http://%s on device\n", STATIC_IP);
}

// ─── Serial command handler ───────────────────────────────────────────
// Commands (send via Serial Monitor, line ending = newline):
//   tare_rum              — tare with empty scale
//   calibrate_rum <grams> — calibrate with known weight on scale
//   rum_raw               — print current reading in kg
static String serial_buf;

static void handle_serial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            serial_buf.trim();
            if (serial_buf.length() == 0) { serial_buf = ""; return; }

            if (serial_buf == "tare_rum") {
                if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
                    rum_tare();
                    xSemaphoreGive(hx711_mutex);
                    Serial.println("[serial] tare_rum done");
                }
            } else if (serial_buf.startsWith("calibrate_rum ")) {
                float grams = serial_buf.substring(14).toFloat();
                if (grams > 0) {
                    if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(8000))) {
                        bool ok = rum_calibrate(grams / 1000.0f);
                        xSemaphoreGive(hx711_mutex);
                        Serial.printf("[serial] calibrate_rum %s\n", ok ? "OK" : "FAILED");
                    }
                } else {
                    Serial.println("[serial] usage: calibrate_rum <grams>");
                }
            } else if (serial_buf == "rum_raw") {
                if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
                    float kg = rum_get_raw_units();
                    xSemaphoreGive(hx711_mutex);
                    Serial.printf("[serial] rum_raw = %.4f kg\n", kg);
                }
            } else {
                Serial.printf("[serial] unknown: %s\n", serial_buf.c_str());
            }
            serial_buf = "";
        } else {
            serial_buf += c;
        }
    }
}

// Suorittaa deep sleepin. Kutsutaan vain loop():ista, ei koskaan HTTP-
// käsittelijästä. Paluuta ei ole — laite herää tästä setup():iin.
static void enter_deep_sleep(uint32_t seconds) {
    // Selite RTC-muistiin, jotta aamun /data kertoo miksi laite buuttasi.
    // note_restart() asettaa myös magicin, joten esp_deep_sleep_start():in
    // mahdollisesti ajama shutdown-handler ei ylikirjoita tätä.
    note_restart("deepsleep %lus cmd=modem", (unsigned long)seconds);
    Serial.printf("[sleep] deep sleep %lu s\n", (unsigned long)seconds);

    // Deep sleepissä tavallinen GPIO menee korkeaimpedanssiseksi. Jos MAX485:n
    // DE kelluu ylös, lähetin ajaa RS485-väylää koko yön — turhaa kulutusta ja
    // väyläkonflikti. GPIO4 on RTC-GPIO, joten tila voidaan lukita.
    digitalWrite(WIND_DE_PIN, LOW);
    rtc_gpio_hold_en((gpio_num_t)WIND_DE_PIN);

    // HX711:n SCK-pinnit (18 vesi, 14 polttoaine, 23 rommi) eivät ole
    // RTC-GPIO:ita eikä niitä voi lukita. Vaa'at jäävät normaalitilaan —
    // muutaman milliampeerin kustannus.

    WiFi.disconnect(true);

    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    Serial.flush();
    esp_deep_sleep_start();
}

// ─── Loop ─────────────────────────────────────────────────────────────
static unsigned long _lastWifiCheck = 0;

void loop() {
    esp_task_wdt_reset();

    ArduinoOTA.handle();

    // Unipyynnön määräaika. Erotus lasketaan etumerkillisenä, jotta millis()-
    // kierähdys ei laukaise unta ennenaikaisesti — sama vertailumuoto kuin
    // liveness-watchdogissa alempana, mutta eri syystä: siellä bugi oli
    // lukujärjestyksessä (näyte otettiin ennen vertailtavan arvon lukua),
    // tässä sleep_at_ms on kiinteä määräaika eikä liikkuva näyte, joten
    // vastaavaa vaaraa ei ole.
    if (sleep_at_ms != 0 && (int32_t)(millis() - sleep_at_ms) >= 0) {
        uint32_t s = pending_sleep_s;
        sleep_at_ms     = 0;
        pending_sleep_s = 0;
        if (ota_active) {                     // OTA alkoi vastauksen jälkeen
            Serial.println("[sleep] peruttu: OTA käynnistyi odotusikkunassa");
            return;
        }
        enter_deep_sleep(s);   // ei palaa
    }

    if (millis() - _lastWifiCheck > 30000) {
        _lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[wifi] yhteys poikki — yritetaan uudelleen");
            WiFi.reconnect();
        } else {
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        }
    }

    // HTTP-vasteaika-watchdog: kattaa hangit loop()-tehtävän ulkopuolella
    // (esim. AsyncTCP/colight_mutex-deadlock), joita hardware WDT ei näe
    // koska tämä silmukka jatkaa tikitystä normaalisti sellaisen aikana.
    // Huom: tämä nojaa siihen että jokin ulkopuolinen taho (susieq-sensors.sh
    // cron / modeemin dashboard-palvelin) pollaa /data:a säännöllisesti —
    // liveness_restart_count rajaa vahingon jos pollaus itsessään pysähtyy
    // modeemin puolella (ks. määrittelyn kommentti ylempänä). Hyökkääjä
    // SusieQ-Net-verkosta ei voi laukaista tätä pelkällä
    // /colight-liikenteellä (/data ei koske colight_mutex:iin).
    // last_data_request_ms LUETAAN ENNEN millis()-näytettä: /data-käsittelijä
    // (AsyncTCP-task, toinen core) voi kirjoittaa muuttujaan uudemman arvon
    // kuin jo otettu millis-näyte, jolloin unsigned-erotus alivuotaa ~4,29
    // miljardiin ja terve laite boottaa itsensä (juurisyy 2026-07-05..07
    // boottailuun, todistettu restart_notella: millis=512204 last_req=512202).
    // Tässä järjestyksessä luettu last on aina ≤ sen jälkeen otettu millis.
    uint32_t liveness_last = last_data_request_ms;
    uint32_t liveness_now  = millis();
    if (WiFi.status() == WL_CONNECTED && !ota_active &&
        liveness_now - liveness_last > HTTP_LIVENESS_TIMEOUT_MS) {
        if (liveness_restart_count < LIVENESS_RESTART_LIMIT) {
            liveness_restart_count++;
            Serial.printf("[watchdog] ei /data-pyyntoa 180s WiFi:n ollessa yhdistettynä — restart (%u/%d)\n",
                          liveness_restart_count, LIVENESS_RESTART_LIMIT);
            note_restart("liveness millis=%lu last_req=%lu count=%u",
                         (unsigned long)liveness_now, (unsigned long)liveness_last,
                         (unsigned)liveness_restart_count);
            Serial.flush();
            esp_restart();
        }
        static bool liveness_gave_up_logged = false;
        if (!liveness_gave_up_logged) {
            liveness_gave_up_logged = true;
            Serial.println("[watchdog] liveness-restart-raja täynnä — pollaus lienee poikki modeemin päässä, ei enää boottailla");
        }
    }

    handle_serial();

    unsigned long now = millis();
    if (now - last_sensor_read >= SENSOR_INTERVAL_MS) {
        last_sensor_read = now;
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(50))) {
            cached_json = build_json();
            xSemaphoreGive(hx711_mutex);
        }
    }
}

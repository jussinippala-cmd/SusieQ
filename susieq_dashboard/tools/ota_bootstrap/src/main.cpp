// OTA watchdog bootstrap — WiFi STA + ArduinoOTA only, ei watchdogia.
// Tarkoitettu vain väliaikaiseksi väliohjelmaksi kun kohdelaitteen
// nykyinen firmware watchdog-resetoi kesken varsinaisen (ison) dashboard-
// firmwaren OTA-siirron. Ei sensoreita, ei web-serveriä, ei BLE:tä —
// pieni koko mahtuu vanhan firmwaren watchdog-ikkunaan, ja kun tämä
// pyörii, seuraava OTA-siirto ei ole enää aikarajoitettu.
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

static const char* STA_SSID     = "SusieQ-Net";
static const char* STA_PASSWORD = "susieq123";
static const char* OTA_HOSTNAME = "susieq-cockpit";
static const char* OTA_PASSWORD = "susieq_ota";

static const IPAddress STATIC_IP(192, 168, 8, 100);
static const IPAddress GATEWAY_IP(192, 168, 8, 1);
static const IPAddress SUBNET_MASK(255, 255, 255, 0);

void setup() {
    Serial.begin(115200);
    Serial.println("\n[bootstrap] OTA watchdog bootstrap kaynnissa");

    WiFi.mode(WIFI_STA);
    WiFi.config(STATIC_IP, GATEWAY_IP, SUBNET_MASK);
    WiFi.begin(STA_SSID, STA_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print(".");
    }
    Serial.printf("\n[bootstrap] WiFi OK, IP: %s\n", WiFi.localIP().toString().c_str());

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        Serial.println("[bootstrap] OTA-siirto alkoi...");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[bootstrap] OTA-siirto valmis - rebootataan");
    });
    ArduinoOTA.onError([](ota_error_t err) {
        Serial.printf("[bootstrap] OTA-virhe %u\n", err);
    });
    ArduinoOTA.begin();
    Serial.println("[bootstrap] odotetaan varsinaisen firmwaren OTA-siirtoa...");
}

void loop() {
    ArduinoOTA.handle();
    delay(5);
}

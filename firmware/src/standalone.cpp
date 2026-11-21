#include "standalone.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>

#include "net.h"
#include "ui_common.h"

namespace {

constexpr uint32_t WEATHER_EVERY_MS = 30 * 60 * 1000;
constexpr uint32_t WEATHER_RETRY_MS = 5 * 60 * 1000;

// Defaults until a host tells us where we are: Cachoeiro de Itapemirim, UTC-3.
float lat = -20.85f;
float lon = -41.11f;
char city[40] = "Cachoeiro de Itapemirim";
int32_t offset = -3 * 3600;

bool ntpStarted = false;
bool ntpLogged = false;
bool fetchedOnce = false;
uint32_t fetchedAt = 0;
uint32_t triedAt = 0;

// Filled by the fetch task, read by the loop once `ready` is set.
struct Result {
    bool ok;
    float temp, min, max;
    int code;
    int32_t offset;
};
Result result;
volatile bool fetching = false;
volatile bool ready = false;
char url[256];

void save() {
    Preferences p;
    p.begin("clock", false);
    p.putFloat("lat", lat);
    p.putFloat("lon", lon);
    p.putString("city", city);
    p.putInt("offset", offset);
    p.end();
}

// Runs on core 0 so a slow network never freezes the screen. The stack is freed when it ends.
void fetchTask(void*) {
    Result r = {};
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(5000);
    if (http.begin(client, url)) {
        if (http.GET() == 200) {
            JsonDocument doc;
            if (deserializeJson(doc, http.getString()) == DeserializationError::Ok && doc["current"]["temperature_2m"].is<float>()) {
                r.temp = doc["current"]["temperature_2m"].as<float>();
                r.code = doc["current"]["weather_code"] | -1;
                r.min = doc["daily"]["temperature_2m_min"][0] | r.temp;
                r.max = doc["daily"]["temperature_2m_max"][0] | r.temp;
                r.offset = doc["utc_offset_seconds"] | offset;
                r.ok = true;
            }
        }
        http.end();
    }
    result = r;
    ready = true;
    fetching = false;
    vTaskDelete(nullptr);
}

void startFetch() {
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min"
             "&timezone=auto&forecast_days=1",
             lat, lon);
    triedAt = millis();
    fetching = true;
    if (xTaskCreatePinnedToCore(fetchTask, "weather", 8192, nullptr, 1, nullptr, 0) != pdPASS) fetching = false;
}

} // namespace

void standaloneBegin() {
    Preferences p;
    p.begin("clock", true);
    lat = p.getFloat("lat", lat);
    lon = p.getFloat("lon", lon);
    p.getString("city", city, sizeof(city));
    offset = p.getInt("offset", offset);
    p.end();
}

void standaloneLoop() {
    if (ready) {
        ready = false;
        // On the USB ports only (hosts ignore "log"); handy with a serial monitor.
        Serial.printf("{\"t\":\"log\",\"msg\":\"weather %s: %.1f C, code %d, offset %ld\"}\n",
                      result.ok ? "ok" : "failed", result.temp, result.code, (long)result.offset);
        if (result.ok) {
            fetchedOnce = true;
            fetchedAt = millis();
            standaloneSetOffset(result.offset);
            clockShowWeather(result.temp, result.min, result.max, result.code, city);
        }
    }
    if (netState() != NetState::connected) return;
    if (!ntpStarted) {
        ntpStarted = true;
        configTime(0, 0, "pool.ntp.org", "time.google.com"); // UTC; the offset is ours. Resyncs hourly.
    }
    if (!ntpLogged && time(nullptr) > 1700000000) {
        ntpLogged = true;
        Serial.printf("{\"t\":\"log\",\"msg\":\"time set: %ld UTC\"}\n", (long)time(nullptr));
    }
    if (fetching) return;
    const uint32_t now = millis();
    const bool due = !fetchedOnce || now - fetchedAt > WEATHER_EVERY_MS;
    const bool mayTry = triedAt == 0 || now - triedAt > WEATHER_RETRY_MS;
    if (due && mayTry) startFetch();
}

void standaloneSetLocation(float newLat, float newLon, const char* newCity) {
    const bool moved = fabsf(newLat - lat) > 0.001f || fabsf(newLon - lon) > 0.001f;
    const bool renamed = newCity && *newCity && strcmp(newCity, city) != 0;
    if (!moved && !renamed) return; // NVS writes stall the flash cache: only on real changes
    lat = newLat;
    lon = newLon;
    if (renamed) snprintf(city, sizeof(city), "%s", newCity);
    save();
    if (moved) {
        fetchedOnce = false; // fetch the new place's weather now
        triedAt = 0;
    }
}

void standaloneSetOffset(int32_t seconds) {
    if (seconds == offset) return;
    offset = seconds;
    save();
    clockRefresh();
}

int32_t standaloneOffset() { return offset; }

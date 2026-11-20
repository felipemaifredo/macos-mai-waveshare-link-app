// AI Usage display: shows Claude/Antigravity usage sent by the Mac app over USB.
//
// Protocol: one JSON object per line, on both USB ports (native "USB" and "UART").
//   Board → Mac: {"t":"hello"} every second until the Mac answers; {"t":"press","id":"refresh"}
//                and {"t":"press","id":"key:<id>"} from the deck page (see KEYS in ui.cpp)
//   Mac → Board: {"t":"usage","providers":[…]} and {"t":"ping"} every 2 s
#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

#include "display.h"
#include "ui.h"

namespace {

struct Port {
    Stream* stream;
    String line;
};
Port ports[] = {{&Serial, ""}, {&Serial0, ""}};

constexpr uint32_t LINK_TIMEOUT_MS = 8000;
bool linked = false;
uint32_t lastRx = 0;
uint32_t lastHello = 0;

void sendLine(const char* line) {
    for (Port& p : ports) {
        p.stream->print(line);
        p.stream->print('\n');
    }
}

void sendPress(const char* id) {
    JsonDocument doc;
    doc["t"] = "press";
    doc["id"] = id;
    char out[96];
    serializeJson(doc, out, sizeof(out));
    sendLine(out);
}

void handleLine(const String& line) {
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) return;
    const char* t = doc["t"] | "";
    if (!*t) return;

    lastRx = millis();
    if (!linked) {
        linked = true;
        uiSetLinked(true);
    }
    if (strcmp(t, "usage") == 0) uiShowUsage(doc["providers"].as<JsonArrayConst>());
}

void pollPorts() {
    for (Port& p : ports) {
        while (p.stream->available()) {
            const char c = p.stream->read();
            if (c == '\n') {
                handleLine(p.line);
                p.line = "";
            } else if (c != '\r' && p.line.length() < 16384) {
                p.line += c;
            }
        }
    }
}

} // namespace

void setup() {
    Serial.setRxBufferSize(8192);
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0); // never block when no host is listening
    Serial0.setRxBufferSize(8192);
    Serial0.begin(115200);

    displayBegin();
    uiBegin(sendPress);
    if (!touchFound()) sendLine("{\"t\":\"log\",\"msg\":\"GT911 touch not found\"}");
}

void loop() {
    pollPorts();

    const uint32_t now = millis();
    if (linked && now - lastRx > LINK_TIMEOUT_MS) {
        linked = false;
        uiSetLinked(false);
    }
    if (!linked && now - lastHello > 1000) {
        lastHello = now;
        sendLine("{\"t\":\"hello\",\"dev\":\"ai-usage-display\",\"v\":1}");
    }

    lv_timer_handler();
    delay(5);
}

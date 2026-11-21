// Waveshare Link display: shows what the computers ("hosts") send, over USB and Wi-Fi.
//
// Every transport carries the same protocol: one JSON object per line (see README, "Protocolo").
// Hosts: the two USB ports (native "USB" and "UART") and up to 3 Wi-Fi clients on TCP port 7270.
// Several hosts can be connected at once; only the active one gets presses and page changes, and only
// its data reaches the screen. The chip in the header switches hosts.
//
// Wi-Fi hosts must authenticate before anything else: a paired host proves it knows its token with
// an HMAC over this connection's nonce (the token never crosses the network again), and the board
// proves the same back. A new host pairs by typing the 6-digit code the board shows.
// USB hosts need nothing: physical access is trust enough. Old (v1) hosts work over USB only.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <lvgl.h>
#include <mbedtls/md.h>

#include "display.h"
#include "net.h"
#include "standalone.h"
#include "ui.h"

namespace {

constexpr uint32_t LINK_TIMEOUT_MS = 8000;  // a linked host that sends nothing for this long is gone
constexpr uint32_t AUTH_TIMEOUT_MS = 90000; // a Wi-Fi client has this long to authenticate or pair
constexpr size_t MAX_LINE = 49152;          // cover art lines are ~20 KB
constexpr int USB_HOSTS = 2;
constexpr int MAX_HOSTS = USB_HOSTS + 3;
constexpr int MAX_PAIRED = 4;
constexpr uint8_t MAX_PAIR_TRIES = 3;

enum class Via : uint8_t { usb, uart, wifi };

struct Host {
    Via via = Via::wifi;
    Stream* stream = nullptr;
    WiFiClient client;
    String line;
    bool open = false;   // USB: always; Wi-Fi: socket connected
    bool linked = false; // counts as a host (authenticated, or any message on USB)
    bool v2 = false;     // sent auth: understands active/pong/net
    char name[24] = "";
    char hostId[40] = "";
    char nonce[17] = ""; // Wi-Fi: this connection's challenge
    uint32_t openedAt = 0;
    uint32_t lastRx = 0;
    uint32_t lastHello = 0;
};
Host hosts[MAX_HOSTS];
int active = -1;              // the host on screen
int listSlots[MAX_HOSTS];     // the host list shown by the UI → slots in `hosts`
int pairing = -1;             // the host being shown a pairing code
char pairCode[7] = "";
uint8_t pairTries = 0;
uint32_t lastNetText = 0;
uint32_t bootDone = 0;
bool heapLogged = false;

// Paired Wi-Fi hosts, kept in NVS.
struct Paired {
    char hostId[40];
    char name[24];
    char token[33];
};
Paired paired[MAX_PAIRED];
int pairedCount = 0;

// --- Crypto helpers -------------------------------------------------------------

void randomHex(char* out, size_t bytes) {
    for (size_t i = 0; i < bytes; i++) snprintf(out + i * 2, 3, "%02x", (uint8_t)esp_random());
}

void hmacHex(const char* key, const char* prefix, const char* msg, char out[65]) {
    String m = String(prefix) + msg;
    uint8_t mac[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t*)key, strlen(key),
                    (const uint8_t*)m.c_str(), m.length(), mac);
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", mac[i]);
}

bool sameSecret(const char* a, const char* b) {
    const size_t n = strlen(a);
    if (n != strlen(b)) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

// --- Paired hosts -----------------------------------------------------------------

void loadPaired() {
    Preferences p;
    p.begin("hosts", true);
    pairedCount = min<int>(p.getUChar("n", 0), MAX_PAIRED);
    for (int i = 0; i < pairedCount; i++) {
        char key[4];
        snprintf(key, sizeof(key), "h%d", i);
        if (p.getBytes(key, &paired[i], sizeof(Paired)) != sizeof(Paired)) {
            pairedCount = i;
            break;
        }
    }
    p.end();
}

void savePaired() {
    Preferences p;
    p.begin("hosts", false);
    p.clear();
    p.putUChar("n", pairedCount);
    for (int i = 0; i < pairedCount; i++) {
        char key[4];
        snprintf(key, sizeof(key), "h%d", i);
        p.putBytes(key, &paired[i], sizeof(Paired));
    }
    p.end();
}

const Paired* findPaired(const char* hostId) {
    if (!*hostId) return nullptr;
    for (int i = 0; i < pairedCount; i++) {
        if (strcmp(paired[i].hostId, hostId) == 0) return &paired[i];
    }
    return nullptr;
}

// Replaces the entry with the same hostId; when full, the oldest goes.
void storePaired(const Paired& p) {
    int at = -1;
    for (int i = 0; i < pairedCount; i++) {
        if (strcmp(paired[i].hostId, p.hostId) == 0) at = i;
    }
    if (at >= 0) {
        for (int i = at; i < pairedCount - 1; i++) paired[i] = paired[i + 1];
        pairedCount--;
    } else if (pairedCount == MAX_PAIRED) {
        for (int i = 0; i < pairedCount - 1; i++) paired[i] = paired[i + 1];
        pairedCount--;
    }
    paired[pairedCount++] = p;
    savePaired();
}

// --- Sending ----------------------------------------------------------------------

void sendTo(Host& h, const char* line) {
    if (!h.open) return;
    const size_t n = strlen(line);
    if (h.via == Via::wifi) {
        // One write per message: with Nagle off, a separate '\n' would be a packet of its own.
        char buf[512];
        if (n < sizeof(buf)) {
            memcpy(buf, line, n);
            buf[n] = '\n';
            h.client.write((const uint8_t*)buf, n + 1);
        } else {
            h.client.write((const uint8_t*)line, n);
            h.client.write((uint8_t)'\n');
        }
    } else {
        h.stream->print(line);
        h.stream->print('\n');
    }
}

void sendDoc(Host& h, const JsonDocument& doc) {
    char out[512];
    serializeJson(doc, out, sizeof(out));
    sendTo(h, out);
}

void sendHello(Host& h) {
    JsonDocument d;
    d["t"] = "hello";
    d["dev"] = "ai-usage-display";
    d["v"] = 2;
    d["id"] = netBoardId();
    if (h.via == Via::wifi) d["nonce"] = h.nonce;
    sendDoc(h, d);
}

void sendPage(Host& h, const char* id) {
    JsonDocument d;
    d["t"] = "page";
    d["id"] = id;
    sendDoc(h, d);
}

const char* netStateName() {
    switch (netState()) {
        case NetState::connected: return "connected";
        case NetState::connecting: return "connecting";
        default: return "off";
    }
}

void sendNet(Host& h) {
    if (!h.v2) return;
    JsonDocument d;
    d["t"] = "net";
    d["state"] = netStateName();
    d["ssid"] = netSsid();
    d["ip"] = netIp();
    d["rssi"] = netRssi();
    d["reason"] = netReason();
    d["why"] = netReasonText();
    sendDoc(h, d);
}

// The UI calls these: only the active host hears them.
void pressToActive(const char* id) {
    if (active < 0) return;
    JsonDocument d;
    d["t"] = "press";
    d["id"] = id;
    sendDoc(hosts[active], d);
}

void pageToActive(const char* id) {
    if (active >= 0) sendPage(hosts[active], id);
}

// --- Hosts ------------------------------------------------------------------------

const char* displayName(const Host& h) {
    if (*h.name) return h.name;
    return h.via == Via::uart ? "UART" : "USB";
}

void publishHosts() {
    UiHost list[MAX_HOSTS];
    int n = 0, act = -1;
    for (int i = 0; i < MAX_HOSTS; i++) {
        if (!hosts[i].linked) continue;
        list[n] = {displayName(hosts[i]), hosts[i].via == Via::wifi};
        listSlots[n] = i;
        if (i == active) act = n;
        n++;
    }
    uiSetHosts(list, n, act);
}

void activate(int i) {
    if (i == active) return;
    const int old = active;
    active = i;
    if (old >= 0 && hosts[old].linked && hosts[old].v2) sendTo(hosts[old], "{\"t\":\"active\",\"on\":false}");
    uiSetLinked(false); // clears what the previous host left on screen
    if (i >= 0) {
        uiSetLinked(true);
        // A v2 host answers with its layout and the state of every page.
        if (hosts[i].v2) sendTo(hosts[i], "{\"t\":\"active\",\"on\":true}");
        sendPage(hosts[i], uiCurrentPage());
    }
    publishHosts();
    uiSetStandby(active < 0);
}

void link(int i) {
    Host& h = hosts[i];
    h.linked = true;
    h.lastRx = millis();
    if (active < 0) {
        activate(i);
    } else {
        if (h.v2) sendTo(h, "{\"t\":\"active\",\"on\":false}");
        publishHosts();
    }
}

void endPairing() {
    pairing = -1;
    pairCode[0] = 0;
    uiShowPairCode(nullptr, nullptr);
}

// Forgets a host. A Wi-Fi socket is closed; a USB port stays open and says hello again.
void drop(int i) {
    Host& h = hosts[i];
    const bool wasLinked = h.linked;
    h.linked = false;
    h.v2 = false;
    h.name[0] = 0;
    h.hostId[0] = 0;
    if (i == pairing) endPairing();
    if (h.via == Via::wifi && h.open) {
        h.client.stop();
        h.open = false;
        h.line = String(); // frees the 48 KB buffer
    }
    if (!wasLinked) return;
    if (i == active) {
        int next = -1;
        for (int j = 0; j < MAX_HOSTS && next < 0; j++) {
            if (hosts[j].linked) next = j;
        }
        activate(next);
    } else {
        publishHosts();
    }
}

void onAuth(int i, const JsonDocument& doc) {
    Host& h = hosts[i];
    const char* hostId = doc["hostId"] | "";
    JsonDocument r;
    r["t"] = "auth";
    if (h.via == Via::wifi) {
        const Paired* p = findPaired(hostId);
        char expected[65];
        if (p) hmacHex(p->token, "host:", h.nonce, expected);
        if (!p || !sameSecret(expected, doc["hmac"] | "")) {
            r["ok"] = false;
            sendDoc(h, r);
            return;
        }
        char proof[65];
        hmacHex(p->token, "board:", doc["nonce"] | "", proof);
        r["hmac"] = proof;
    }
    r["ok"] = true;
    r["id"] = netBoardId();
    sendDoc(h, r);

    snprintf(h.name, sizeof(h.name), "%s", doc["name"] | "Computador");
    snprintf(h.hostId, sizeof(h.hostId), "%s", hostId);
    h.v2 = true;
    if (!h.linked) {
        link(i);
    } else {
        // Already linked (e.g. the app restarted over USB): tell it where it stands.
        sendTo(h, i == active ? "{\"t\":\"active\",\"on\":true}" : "{\"t\":\"active\",\"on\":false}");
        if (i == active) sendPage(h, uiCurrentPage());
        publishHosts();
    }
    sendNet(h);
}

void onPair(int i, const JsonDocument& doc) {
    Host& h = hosts[i];
    if (h.via != Via::wifi || h.linked) return;
    const char* code = doc["code"] | "";
    JsonDocument r;
    r["t"] = "pair";

    if (!*code) { // asking for a code
        if (pairing >= 0 && pairing != i) {
            r["state"] = "busy";
            sendDoc(h, r);
            return;
        }
        snprintf(h.name, sizeof(h.name), "%s", doc["name"] | "Computador");
        snprintf(h.hostId, sizeof(h.hostId), "%s", doc["hostId"] | "");
        if (pairing != i) {
            pairing = i;
            pairTries = 0;
            snprintf(pairCode, sizeof(pairCode), "%06lu", (unsigned long)(esp_random() % 1000000));
        }
        uiShowPairCode(pairCode, h.name);
        r["state"] = "code";
        sendDoc(h, r);
        return;
    }

    if (pairing != i || !*h.hostId) {
        r["state"] = "nocode";
        sendDoc(h, r);
        return;
    }
    if (!sameSecret(code, pairCode)) {
        if (++pairTries >= MAX_PAIR_TRIES) {
            r["state"] = "failed";
            sendDoc(h, r);
            drop(i);
        } else {
            r["state"] = "wrong";
            sendDoc(h, r);
        }
        return;
    }

    Paired p = {};
    snprintf(p.hostId, sizeof(p.hostId), "%s", h.hostId);
    snprintf(p.name, sizeof(p.name), "%s", h.name);
    randomHex(p.token, 16);
    storePaired(p);
    endPairing();

    JsonDocument ok;
    ok["t"] = "paired";
    ok["token"] = p.token;
    ok["id"] = netBoardId();
    sendDoc(h, ok);
    h.v2 = true;
    link(i);
    sendNet(h);
}

void handleLine(int i, const String& line) {
    Host& h = hosts[i];
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) return;
    const char* t = doc["t"] | "";
    if (!*t) return;
    h.lastRx = millis();

    if (strcmp(t, "probe") == 0) { // a host that just opened the port and missed the hellos
        sendHello(h);
        return;
    }
    if (strcmp(t, "auth") == 0) return onAuth(i, doc);
    if (strcmp(t, "pair") == 0) return onPair(i, doc);
    if (strcmp(t, "bye") == 0) return drop(i);
    if (strcmp(t, "wifi") == 0) {
        // Only over USB: whoever holds the cable may change the network.
        if (h.via != Via::wifi) netConfigure(doc["ssid"] | "", doc["pass"] | "");
        return;
    }

    if (!h.linked) {
        if (h.via == Via::wifi) return; // must authenticate first
        link(i);                        // USB v1 hosts: any message links
    }
    if (strcmp(t, "ping") == 0) {
        if (h.v2) sendTo(h, "{\"t\":\"pong\"}");
        return;
    }
    if (strcmp(t, "name") == 0) { // the user renamed the host in its settings
        snprintf(h.name, sizeof(h.name), "%s", doc["name"] | "");
        publishHosts();
        return;
    }
    if (i != active) return; // inactive hosts only keep the link alive

    JsonObjectConst msg = doc.as<JsonObjectConst>();
    if (strcmp(t, "usage") == 0) uiShowUsage(doc["providers"].as<JsonArrayConst>());
    else if (strcmp(t, "now") == 0) uiShowNow(msg);
    else if (strcmp(t, "art") == 0) uiShowArt(doc["jpg"] | "");
    else if (strcmp(t, "mac") == 0) uiShowMac(msg);
    else if (strcmp(t, "docker") == 0) uiShowDocker(msg);
    else if (strcmp(t, "clock") == 0) uiShowClock(msg);
    else if (strcmp(t, "audio") == 0) uiShowAudio(msg);
    else if (strcmp(t, "discord") == 0) uiShowDiscord(msg);
    else if (strcmp(t, "mic") == 0) uiShowMic(msg);
    else if (strcmp(t, "layout") == 0) uiShowLayout(msg);
}

size_t readSome(Host& h, uint8_t* buf, size_t n) {
    if (h.via == Via::wifi) {
        const int r = h.client.read(buf, n);
        return r > 0 ? r : 0;
    }
    return h.stream->readBytes(buf, n);
}

void pollHosts() {
    static uint8_t buf[2048];
    for (int i = 0; i < MAX_HOSTS; i++) {
        Host& h = hosts[i];
        while (h.open) {
            const int avail = h.stream->available();
            if (avail <= 0) break;
            const size_t n = readSome(h, buf, min<size_t>(avail, sizeof(buf)));
            if (n == 0) break;
            for (size_t k = 0; k < n && h.open; k++) {
                const char c = buf[k];
                if (c == '\n') {
                    handleLine(i, h.line);
                    h.line = "";
                } else if (c != '\r' && h.line.length() < MAX_LINE) {
                    h.line += c;
                }
            }
        }
    }
}

void acceptClients() {
    WiFiClient c = netAccept();
    if (!c) return;
    for (int i = USB_HOSTS; i < MAX_HOSTS; i++) {
        Host& h = hosts[i];
        if (h.open) continue;
        h.client = c;
        h.client.setNoDelay(true);
        h.stream = &h.client;
        h.open = true;
        h.linked = false;
        h.v2 = false;
        h.name[0] = 0;
        h.hostId[0] = 0;
        h.openedAt = h.lastRx = millis();
        h.line = "";
        h.line.reserve(MAX_LINE);
        randomHex(h.nonce, 8);
        sendHello(h);
        return;
    }
    c.stop(); // no free slot
}

void updateNetText() {
    char text[128];
    switch (netState()) {
        case NetState::connected:
            snprintf(text, sizeof(text), "Wi-Fi: %s   %s   %d dBm", netSsid(), netIp().c_str(), netRssi());
            break;
        case NetState::connecting:
            if (netReason()) snprintf(text, sizeof(text), "Wi-Fi \"%s\": %s (%d)", netSsid(), netReasonText(), netReason());
            else snprintf(text, sizeof(text), "Wi-Fi: conectando a \"%s\"...", netSsid());
            break;
        default:
            snprintf(text, sizeof(text), "Wi-Fi: desligado (configure pelo app, com o cabo USB)");
    }
    uiSetNet(text);
}

// --- UI callbacks -------------------------------------------------------------

void onHostSelected(int index) {
    const int slot = listSlots[index];
    if (slot >= 0 && slot < MAX_HOSTS && hosts[slot].linked) activate(slot);
}

void onPairCancel() {
    if (pairing < 0) return;
    sendTo(hosts[pairing], "{\"t\":\"pair\",\"state\":\"cancelled\"}");
    drop(pairing);
}

void onForgetHosts() {
    pairedCount = 0;
    savePaired();
    for (int i = USB_HOSTS; i < MAX_HOSTS; i++) {
        if (hosts[i].open) drop(i);
    }
}

} // namespace

// Free internal RAM (the Wi-Fi driver, lwIP and the LVGL draw buffers need it; PSRAM won't do).
void logHeap(const char* stage) {
    char msg[128];
    snprintf(msg, sizeof(msg), "{\"t\":\"log\",\"msg\":\"heap %s: internal free %u, largest %u, psram free %u\"}", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    for (int i = 0; i < USB_HOSTS; i++) sendTo(hosts[i], msg);
}

void setup() {
    // Internal RAM is scarce and the Wi-Fi driver must have it. By default only blocks of 4 KB or
    // more go to PSRAM, and the LVGL objects ate the internal heap (Wi-Fi failed with ESP_ERR_NO_MEM).
    // Sending everything from 64 bytes up to PSRAM fixed that but broke the picture: the RGB panel
    // streams its frame from PSRAM, and LVGL hammering PSRAM on every redraw starved it (lines
    // glitching now and then). Middle ground: small objects (the many lv_obj/styles touched on every
    // redraw) stay internal, bigger blocks go to PSRAM.
    heap_caps_malloc_extmem_enable(1024);

    // USB CDC has flow control and the UART is slow (11.5 KB/s): 8 KB is plenty for the loop's pace.
    Serial.setRxBufferSize(8192);
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0); // never block when no host is listening
    Serial0.setRxBufferSize(8192);
    Serial0.begin(115200);

    hosts[0].via = Via::usb;
    hosts[0].stream = &Serial;
    hosts[1].via = Via::uart;
    hosts[1].stream = &Serial0;
    for (int i = 0; i < USB_HOSTS; i++) {
        hosts[i].open = true;
        hosts[i].line.reserve(MAX_LINE);
    }

    // Wi-Fi first, while internal RAM is still free; the UI's objects can live in PSRAM.
    loadPaired();
    standaloneBegin();
    netBegin();
    displayBegin();
    uiBegin(pressToActive, pageToActive);
    uiSetHostHandlers(onHostSelected, onPairCancel, onForgetHosts);
    updateNetText();
    bootDone = millis();
    if (!touchFound()) {
        for (int i = 0; i < USB_HOSTS; i++) sendTo(hosts[i], "{\"t\":\"log\",\"msg\":\"GT911 touch not found\"}");
    }
}

void loop() {
    netLoop();
    standaloneLoop();
    acceptClients();
    pollHosts();

    const uint32_t now = millis();
    for (int i = 0; i < MAX_HOSTS; i++) {
        Host& h = hosts[i];
        if (!h.open) continue;
        if (h.via == Via::wifi && !h.client.connected()) {
            drop(i);
        } else if (h.linked && now - h.lastRx > LINK_TIMEOUT_MS) {
            drop(i);
        } else if (!h.linked && h.via == Via::wifi && now - h.openedAt > AUTH_TIMEOUT_MS) {
            drop(i);
        } else if (!h.linked && h.via != Via::wifi && now - h.lastHello > 1000) {
            h.lastHello = now;
            sendHello(h);
        }
    }

    if (!heapLogged && millis() - bootDone > 5000) { // once the host can hear it
        heapLogged = true;
        logHeap("5s after boot");
    }
    if (netChanged()) {
        updateNetText();
        lastNetText = now;
        for (Host& h : hosts) {
            if (h.linked) sendNet(h);
        }
    } else if (now - lastNetText > 5000) { // signal strength
        updateNetText();
        lastNetText = now;
    }

    lv_timer_handler();
    delay(5);
}

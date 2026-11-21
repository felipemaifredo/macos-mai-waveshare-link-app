#include "net.h"

#include <ESPmDNS.h>
#include <Preferences.h>

namespace {

constexpr uint32_t RETRY_MS = 30000; // a connection attempt that has not worked by then starts over

char ssid[33] = "";
char pass[65] = "";
char boardId[13] = "";
NetState state = NetState::off;
bool changed = true;
bool serverUp = false;
bool mdnsUp = false;
uint32_t attemptAt = 0;
String lastIp;
WiFiServer server(NET_PORT, 4);
// Why the last attempt failed (WIFI_REASON_*), set from the Wi-Fi task. 0: none yet.
volatile uint8_t lastReason = 0;
volatile bool reasonChanged = false;

void loadCredentials() {
    Preferences p;
    p.begin("wifi", true);
    p.getString("ssid", ssid, sizeof(ssid));
    p.getString("pass", pass, sizeof(pass));
    p.end();
}

void connect() {
    attemptAt = millis();
    WiFi.disconnect(false, false);
    // Hidden networks do not answer broadcast probes: scanning every channel for the saved
    // SSID by name finds them (a fast scan stops at the first match and can miss them).
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    WiFi.begin(ssid, pass);
}

void setState(NetState s) {
    if (s != state) {
        state = s;
        changed = true;
    }
}

} // namespace

void netBegin() {
    const uint64_t mac = ESP.getEfuseMac();
    snprintf(boardId, sizeof(boardId), "%02X%02X%02X%02X%02X%02X", (uint8_t)mac, (uint8_t)(mac >> 8),
             (uint8_t)(mac >> 16), (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));

#ifdef WAVESHARE_NO_WIFI
    return; // diagnostic build: the radio stays off (the saved network is kept)
#endif
    loadCredentials();
    WiFi.persistent(false); // the credentials live in our own NVS namespace
    WiFi.setHostname("waveshare"); // before mode(): the core names the interface when it creates it
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // modem sleep adds 100+ ms to every packet (sliders, presses)
    WiFi.setAutoReconnect(true);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
        const uint8_t r = info.wifi_sta_disconnected.reason;
        if (r == WIFI_REASON_ASSOC_LEAVE) return; // our own disconnect() before a new attempt
        if (r != lastReason) reasonChanged = true;
        lastReason = r;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    if (*ssid) {
        connect();
        setState(NetState::connecting);
    }
}

void netLoop() {
    if (!*ssid) {
        setState(NetState::off);
        return;
    }
    if (reasonChanged) {
        reasonChanged = false;
        changed = true;
    }
    if (WiFi.isConnected()) {
        lastReason = 0;
        setState(NetState::connected);
        const String ip = WiFi.localIP().toString();
        if (ip != lastIp) {
            lastIp = ip;
            changed = true;
        }
        if (!serverUp) {
            server.begin();
            server.setNoDelay(true);
            serverUp = true;
        }
        if (!mdnsUp && MDNS.begin("waveshare")) {
            MDNS.addService("wslink", "tcp", NET_PORT);
            MDNS.addServiceTxt("wslink", "tcp", "id", (const char*)boardId);
            mdnsUp = true;
        }
        attemptAt = millis();
        return;
    }
    setState(NetState::connecting);
    // The core reconnects by itself after most drops, but not when the network was not found
    // (e.g. the router was off). Start over every 30 s until it works.
    if (millis() - attemptAt > RETRY_MS) connect();
}

void netConfigure(const char* newSsid, const char* newPass) {
    snprintf(ssid, sizeof(ssid), "%s", newSsid ? newSsid : "");
    snprintf(pass, sizeof(pass), "%s", newPass ? newPass : "");
    Preferences p;
    p.begin("wifi", false);
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.end();

    lastIp = "";
    lastReason = 0;
    if (*ssid) {
        connect();
        setState(NetState::connecting);
    } else {
        WiFi.disconnect(false, true);
        setState(NetState::off);
    }
    changed = true;
}

WiFiClient netAccept() {
    if (!serverUp || !WiFi.isConnected()) return WiFiClient();
    return server.accept();
}

NetState netState() { return state; }
const char* netSsid() { return ssid; }
String netIp() { return state == NetState::connected ? WiFi.localIP().toString() : String(); }
int netRssi() { return state == NetState::connected ? WiFi.RSSI() : 0; }
const char* netBoardId() { return boardId; }
int netReason() { return state == NetState::connected ? 0 : lastReason; }

const char* netReasonText() {
    switch (netReason()) {
        case 0: return "";
        case WIFI_REASON_NO_AP_FOUND:
            return "rede nao encontrada: confira o nome exato e se a rede tem 2,4 GHz";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
            return "senha recusada";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return "seguranca da rede incompativel (use WPA2 ou WPA2/WPA3)";
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        case WIFI_REASON_BEACON_TIMEOUT:
            return "sinal fraco demais";
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_ASSOC_TOOMANY:
            return "o roteador recusou a conexao";
        default:
            return "falha na conexao";
    }
}

bool netChanged() {
    const bool c = changed;
    changed = false;
    return c;
}

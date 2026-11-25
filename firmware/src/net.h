#pragma once

// Wi-Fi (station), the TCP server the hosts connect to and the mDNS announcement.
// The credentials live in NVS and arrive over USB ({"t":"wifi","ssid","pass"}).
// Hidden networks work: the board probes for the saved SSID by name on every channel.

#include <WiFi.h>

constexpr uint16_t NET_PORT = 7270;

enum class NetState : uint8_t { off, connecting, connected };

void netBegin();
// Keeps the connection up and starts the server/mDNS once there is an IP. Call it every loop.
void netLoop();
// Saves new credentials and reconnects. An empty SSID forgets the network.
void netConfigure(const char* ssid, const char* pass);
// A host that just connected, or an invalid client.
WiFiClient netAccept();
// Looks up an IPv4 peer in lwIP's ARP table. The peer must have communicated recently.
bool netFindMac(IPAddress ip, uint8_t mac[6]);
// Sends a Wake-on-LAN magic packet to both the subnet and limited broadcasts.
bool netWake(const uint8_t mac[6]);

NetState netState();
const char* netSsid();
String netIp();
int netRssi();
// True once after the state (or the IP) changes.
bool netChanged();
// Why the board is not connected (WIFI_REASON_* code and a short ASCII text). 0/"" when connected
// or before the first failure.
int netReason();
const char* netReasonText();
// The board's id (its MAC, 12 hex digits), also announced in mDNS.
const char* netBoardId();

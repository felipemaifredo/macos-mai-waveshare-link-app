#pragma once

#include <ArduinoJson.h>

using PressHandler = void (*)(const char* id);
using PageHandler = void (*)(const char* pageId);

// onPage is called with the page id ("usage", "now", "keys", "mac", "docker", "clock")
// whenever the visible page changes, so the Mac only gathers data for that page.
void uiBegin(PressHandler onPress, PageHandler onPage);
const char* uiCurrentPage();
void uiSetLinked(bool linked);

void uiShowUsage(JsonArrayConst providers);
void uiShowNow(JsonObjectConst msg);
void uiShowArt(const char* base64);
void uiShowMac(JsonObjectConst msg);
void uiShowDocker(JsonObjectConst msg);
void uiShowClock(JsonObjectConst msg);
void uiShowAudio(JsonObjectConst msg);
void uiShowDiscord(JsonObjectConst msg);
void uiShowMic(JsonObjectConst msg);
// {"t":"layout","pages":[ids in tab order],"keys":[{"id","icon","text"}]}
void uiShowLayout(JsonObjectConst msg);

// --- Hosts (the computers connected to the board) ----------------------------------

struct UiHost {
    const char* name;
    bool wifi; // false: USB
};
using HostSelectHandler = void (*)(int index); // index into the last uiSetHosts list
using VoidHandler = void (*)();
void uiSetHostHandlers(HostSelectHandler onSelect, VoidHandler onPairCancel, VoidHandler onForgetHosts);
// The connected hosts and which one is on screen (-1: none). The chip in the header shows the
// active one; tapping it lists all of them so the user can switch.
void uiSetHosts(const UiHost* hosts, int count, int active);
// One line about the network for the host list, e.g. "Wi-Fi: Casa  192.168.0.20".
void uiSetNet(const char* text);
// The full-screen clock shown while no computer is connected.
void uiSetStandby(bool on);
// Shows the pairing code a new host must type. nullptr hides it.
void uiShowPairCode(const char* code, const char* hostName);

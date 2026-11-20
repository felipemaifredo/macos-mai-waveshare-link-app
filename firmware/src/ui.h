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
void uiShowMic(JsonObjectConst msg);

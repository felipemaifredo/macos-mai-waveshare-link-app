#pragma once

#include <ArduinoJson.h>

using PressHandler = void (*)(const char* id);

void uiBegin(PressHandler onPress);
void uiSetLinked(bool linked);
void uiShowUsage(JsonArrayConst providers);

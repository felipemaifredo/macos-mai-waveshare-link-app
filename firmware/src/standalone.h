#pragma once

// What the board does on its own, without any computer: the time over NTP and the weather from
// Open-Meteo (plain HTTP: TLS would need ~40 KB of internal RAM the board does not have).
// The location (lat, lon, city) and the UTC offset are kept in NVS; the hosts send the location in
// their "clock" message, and Open-Meteo answers with the offset for it (daylight saving included).

#include <stdint.h>

void standaloneBegin();
// NTP once Wi-Fi is up, and the weather every 30 min. Call it every loop.
void standaloneLoop();
void standaloneSetLocation(float lat, float lon, const char* city);
void standaloneSetOffset(int32_t seconds);
int32_t standaloneOffset();

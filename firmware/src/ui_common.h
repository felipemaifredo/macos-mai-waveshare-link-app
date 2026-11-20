#pragma once

// Shared look and helpers for the pages in ui*.cpp.

#include <ArduinoJson.h>
#include <lvgl.h>

#include "ui.h"

inline const lv_color_t BG = lv_color_hex(0x101114);
inline const lv_color_t CARD = lv_color_hex(0x1B1D22);
inline const lv_color_t TRACK = lv_color_hex(0x2A2D34);
inline const lv_color_t PRESSED = lv_color_hex(0x2E3138);
inline const lv_color_t TEXT = lv_color_hex(0xE8E8EA);
inline const lv_color_t DIM = lv_color_hex(0x8A8F98);
inline const lv_color_t GOOD = lv_color_hex(0x3DBE78);
inline const lv_color_t WARN = lv_color_hex(0xF0A030);
inline const lv_color_t DANGER = lv_color_hex(0xE5484D);
inline const lv_color_t ACCENT = lv_color_hex(0x4C8DF6);
inline const lv_color_t CLAUDE = lv_color_hex(0xD97757);
inline const lv_color_t GEMINI = lv_color_hex(0x4C8DF6);

// Invisible full-width container that sizes to its content.
lv_obj_t* box(lv_obj_t* parent, lv_flex_flow_t flow);
lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text);
// Rounded dark card.
lv_obj_t* card(lv_obj_t* parent);
// Big rounded button with a symbol (48) and an optional caption (20). Sends `id` through the press handler.
lv_obj_t* deckButton(lv_obj_t* parent, const char* id, const char* icon, const char* text);
void sendPress(const char* id);
void showHidden(lv_obj_t* o, bool show);
lv_color_t levelColor(int pct, lv_color_t normal);

// Big digits rendered from the embedded Montserrat TTF.
const lv_font_t* bigFont(int size);

// Pages. Each builds into its tile once and then updates from the Mac's messages.
void nowBuild(lv_obj_t* tile);
void nowShow(JsonObjectConst msg);
void nowShowArt(const char* base64);
void nowSetLinked(bool linked);

void macBuild(lv_obj_t* tile);
void macShow(JsonObjectConst msg);

void dockerBuild(lv_obj_t* tile);
void dockerShow(JsonObjectConst msg);

void clockBuild(lv_obj_t* tile);
void clockShow(JsonObjectConst msg);

void audioBuild(lv_obj_t* tile);
void audioShow(JsonObjectConst msg);
void audioSetLinked(bool linked);

void discordBuild(lv_obj_t* tile);
void discordShow(JsonObjectConst msg);
void discordSetLinked(bool linked);

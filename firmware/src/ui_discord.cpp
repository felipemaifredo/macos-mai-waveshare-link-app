// "Discord": Discord status, mute/unmute, deafen and disconnect actions.
//   {"t":"discord","running":true,"muted":false,"deafened":false}
#include "ui_common.h"

#include <stdio.h>

namespace {

inline const lv_color_t BLURPLE = lv_color_hex(0x5865F2);

lv_obj_t* statusBadge;
lv_obj_t* muteBtn;
lv_obj_t* muteIcon;
lv_obj_t* muteText;

lv_obj_t* deafBtn;
lv_obj_t* deafIcon;
lv_obj_t* deafText;

bool isMuted = false;
bool isDeaf = false;
bool isRunning = false;

void onMuteClicked(lv_event_t*) {
    sendPress("discord:mute");
}

void onDeafClicked(lv_event_t*) {
    sendPress("discord:deaf");
}

} // namespace

void discordBuild(lv_obj_t* tile) {
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_pad_row(tile, 14, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    // Top status card
    lv_obj_t* topCard = card(tile);
    lv_obj_set_size(topCard, LV_PCT(100), 56);
    lv_obj_set_flex_flow(topCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(topCard, 16, 0);

    lv_obj_t* leftBox = box(topCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(leftBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(leftBox, 10, 0);

    label(leftBox, &lv_font_montserrat_20, BLURPLE, LV_SYMBOL_CALL);
    label(leftBox, &lv_font_montserrat_20, TEXT, "Discord");

    statusBadge = label(topCard, &lv_font_montserrat_16, GOOD, "Aberto");

    // Main 2 big control buttons (Mute & Deafen)
    lv_obj_t* mainRow = box(tile, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(mainRow, LV_PCT(100));
    lv_obj_set_flex_grow(mainRow, 1);
    lv_obj_set_style_pad_column(mainRow, 14, 0);

    // Mute Button
    muteBtn = lv_button_create(mainRow);
    lv_obj_set_flex_grow(muteBtn, 1);
    lv_obj_set_height(muteBtn, LV_PCT(100));
    lv_obj_set_style_radius(muteBtn, 16, 0);
    lv_obj_set_style_bg_color(muteBtn, BLURPLE, 0);
    lv_obj_set_style_bg_color(muteBtn, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(muteBtn, 0, 0);
    lv_obj_set_flex_flow(muteBtn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(muteBtn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(muteBtn, 10, 0);
    lv_obj_add_event_cb(muteBtn, onMuteClicked, LV_EVENT_CLICKED, nullptr);

    muteIcon = label(muteBtn, &lv_font_montserrat_48, TEXT, "MIC");
    muteText = label(muteBtn, &lv_font_montserrat_20, TEXT, "Mutar");

    // Deafen Button
    deafBtn = lv_button_create(mainRow);
    lv_obj_set_flex_grow(deafBtn, 1);
    lv_obj_set_height(deafBtn, LV_PCT(100));
    lv_obj_set_style_radius(deafBtn, 16, 0);
    lv_obj_set_style_bg_color(deafBtn, BLURPLE, 0);
    lv_obj_set_style_bg_color(deafBtn, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(deafBtn, 0, 0);
    lv_obj_set_flex_flow(deafBtn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(deafBtn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(deafBtn, 10, 0);
    lv_obj_add_event_cb(deafBtn, onDeafClicked, LV_EVENT_CLICKED, nullptr);

    deafIcon = label(deafBtn, &lv_font_montserrat_48, TEXT, LV_SYMBOL_AUDIO);
    deafText = label(deafBtn, &lv_font_montserrat_20, TEXT, "Ensurdecer");
}

void discordShow(JsonObjectConst msg) {
    isRunning = msg["running"] | false;
    isMuted = msg["muted"] | false;
    isDeaf = msg["deafened"] | false;

    lv_label_set_text(statusBadge, isRunning ? "Aberto" : "Fechado");
    lv_obj_set_style_text_color(statusBadge, isRunning ? GOOD : DIM, 0);

    // Mute state
    lv_obj_set_style_bg_color(muteBtn, isMuted ? DANGER : BLURPLE, 0);
    lv_label_set_text(muteText, isMuted ? "Mutado (Ligar)" : "Mutar");

    // Deafen state
    lv_obj_set_style_bg_color(deafBtn, isDeaf ? DANGER : BLURPLE, 0);
    lv_label_set_text(deafText, isDeaf ? "Ensurdecido" : "Ensurdecer");
}

void discordSetLinked(bool linked) {
    if (linked) {
        lv_obj_remove_state(muteBtn, LV_STATE_DISABLED);
        lv_obj_remove_state(deafBtn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(muteBtn, LV_STATE_DISABLED);
        lv_obj_add_state(deafBtn, LV_STATE_DISABLED);
    }
}

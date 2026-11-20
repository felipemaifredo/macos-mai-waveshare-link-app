// "Mixer": volume sliders for Master, Media and Mic, plus audio output device switcher.
//   {"t":"audio","master":{"vol":75,"muted":false},"media":{"app":"Spotify","vol":60,"muted":false},
//    "mic":{"vol":80,"muted":false},"activeDev":"AirPods Pro","devices":[{"id":"12","name":"Speakers","active":false},...]}
#include "ui_common.h"

#include <stdio.h>
#include <string.h>

namespace {

struct ChannelUi {
    const char* id;
    const char* defaultTitle;
    lv_color_t color;
    lv_obj_t* card;
    lv_obj_t* icon;
    lv_obj_t* title;
    lv_obj_t* slider;
    lv_obj_t* pctLabel;
    lv_obj_t* muteBtn;
    lv_obj_t* muteLabel;
    int lastSentVal;
    uint32_t lastSentAt;
    bool isDragging;
};

ChannelUi channels[3] = {
    {"master", "Master", ACCENT},
    {"media", "Midia", GOOD},
    {"mic", "Microfone", WARN},
};

constexpr int MAX_DEV_BUTTONS = 6;
struct DeviceBtn {
    lv_obj_t* btn;
    lv_obj_t* label;
    char id[16];
    bool active;
};

lv_obj_t* devScroll;
DeviceBtn devButtons[MAX_DEV_BUTTONS];
int devCount = 0;

void onSliderEvent(lv_event_t* e) {
    ChannelUi* ch = static_cast<ChannelUi*>(lv_event_get_user_data(e));
    if (!ch) return;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_RELEASED) {
        int val = lv_slider_get_value(ch->slider);
        lv_label_set_text_fmt(ch->pctLabel, "%d%%", val);

        uint32_t now = millis();
        if (code == LV_EVENT_RELEASED || (now - ch->lastSentAt >= 50 && val != ch->lastSentVal)) {
            ch->lastSentVal = val;
            ch->lastSentAt = now;
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "vol:%s:%d", ch->id, val);
            sendPress(cmd);
        }
    }
}

void onMuteClicked(lv_event_t* e) {
    ChannelUi* ch = static_cast<ChannelUi*>(lv_event_get_user_data(e));
    if (!ch) return;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "mute:%s", ch->id);
    sendPress(cmd);
}

void onDeviceClicked(lv_event_t* e) {
    DeviceBtn* d = static_cast<DeviceBtn*>(lv_event_get_user_data(e));
    if (!d || !d->id[0]) return;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "outdev:%s", d->id);
    sendPress(cmd);
}

lv_obj_t* buildChannel(lv_obj_t* parent, ChannelUi& ch, const char* symbol) {
    ch.card = card(parent);
    lv_obj_set_flex_grow(ch.card, 1);
    lv_obj_set_height(ch.card, LV_PCT(100));
    lv_obj_set_flex_align(ch.card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ch.card, 12, 0);

    // Channel header: icon + name
    lv_obj_t* header = box(ch.card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(header, 4, 0);

    ch.icon = label(header, &lv_font_montserrat_20, ch.color, symbol);
    ch.title = label(header, &lv_font_montserrat_16, TEXT, ch.defaultTitle);
    lv_label_set_long_mode(ch.title, LV_LABEL_LONG_MODE_DOTS);

    // Vertical slider
    ch.slider = lv_slider_create(ch.card);
    lv_slider_set_range(ch.slider, 0, 100);
    lv_slider_set_value(ch.slider, 50, LV_ANIM_OFF);
    lv_obj_set_size(ch.slider, 34, 170);
    lv_obj_set_style_radius(ch.slider, 17, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ch.slider, TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ch.slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ch.slider, 0, LV_PART_MAIN);

    lv_obj_set_style_radius(ch.slider, 17, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ch.slider, ch.color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ch.slider, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_set_style_radius(ch.slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(ch.slider, TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(ch.slider, 6, LV_PART_KNOB);

    lv_obj_add_event_cb(ch.slider, onSliderEvent, LV_EVENT_VALUE_CHANGED, &ch);
    lv_obj_add_event_cb(ch.slider, onSliderEvent, LV_EVENT_RELEASED, &ch);

    // Percentage value
    ch.pctLabel = label(ch.card, &lv_font_montserrat_20, TEXT, "50%");

    // Mute button
    ch.muteBtn = lv_button_create(ch.card);
    lv_obj_set_size(ch.muteBtn, 120, 42);
    lv_obj_set_style_radius(ch.muteBtn, 10, 0);
    lv_obj_set_style_bg_color(ch.muteBtn, TRACK, 0);
    lv_obj_set_style_bg_color(ch.muteBtn, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ch.muteBtn, 0, 0);
    lv_obj_add_event_cb(ch.muteBtn, onMuteClicked, LV_EVENT_CLICKED, &ch);

    ch.muteLabel = label(ch.muteBtn, &lv_font_montserrat_16, TEXT, LV_SYMBOL_VOLUME_MAX " Mudo");
    lv_obj_center(ch.muteLabel);

    return ch.card;
}

} // namespace

void audioBuild(lv_obj_t* tile) {
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_pad_row(tile, 10, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    // Top device bar
    lv_obj_t* devCard = card(tile);
    lv_obj_set_size(devCard, LV_PCT(100), 58);
    lv_obj_set_flex_flow(devCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(devCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(devCard, 4, 0);
    lv_obj_set_style_pad_hor(devCard, 10, 0);
    lv_obj_set_style_pad_column(devCard, 8, 0);

    lv_obj_t* devIcon = label(devCard, &lv_font_montserrat_16, DIM, LV_SYMBOL_AUDIO);
    lv_obj_set_style_margin_right(devIcon, 4, 0);

    devScroll = box(devCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(devScroll, LV_PCT(100));
    lv_obj_set_flex_grow(devScroll, 1);
    lv_obj_set_flex_align(devScroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(devScroll, 8, 0);
    lv_obj_set_scrollbar_mode(devScroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(devScroll, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MAX_DEV_BUTTONS; i++) {
        DeviceBtn& d = devButtons[i];
        d.id[0] = '\0';
        d.active = false;
        d.btn = lv_button_create(devScroll);
        lv_obj_set_height(d.btn, 38);
        lv_obj_set_style_radius(d.btn, 8, 0);
        lv_obj_set_style_bg_color(d.btn, TRACK, 0);
        lv_obj_set_style_bg_color(d.btn, PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(d.btn, 0, 0);
        lv_obj_set_style_pad_hor(d.btn, 12, 0);
        lv_obj_set_style_pad_ver(d.btn, 4, 0);
        lv_obj_add_event_cb(d.btn, onDeviceClicked, LV_EVENT_CLICKED, &d);

        d.label = label(d.btn, &lv_font_montserrat_16, TEXT, "");
        lv_obj_center(d.label);
        showHidden(d.btn, false);
    }

    // Main 3-column sliders area
    lv_obj_t* slidersArea = box(tile, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(slidersArea, LV_PCT(100));
    lv_obj_set_flex_grow(slidersArea, 1);
    lv_obj_set_style_pad_column(slidersArea, 12, 0);
    lv_obj_remove_flag(slidersArea, LV_OBJ_FLAG_SCROLLABLE);

    buildChannel(slidersArea, channels[0], LV_SYMBOL_VOLUME_MAX);
    buildChannel(slidersArea, channels[1], LV_SYMBOL_AUDIO);
    buildChannel(slidersArea, channels[2], "MIC");
}

void audioShow(JsonObjectConst msg) {
    // 1. Devices list
    JsonArrayConst devs = msg["devices"].as<JsonArrayConst>();
    int idx = 0;
    for (JsonObjectConst d : devs) {
        if (idx >= MAX_DEV_BUTTONS) break;
        DeviceBtn& btn = devButtons[idx++];
        const char* id = d["id"] | "";
        const char* name = d["name"] | "";
        bool active = d["active"] | false;

        strncpy(btn.id, id, sizeof(btn.id) - 1);
        btn.id[sizeof(btn.id) - 1] = '\0';
        btn.active = active;

        lv_label_set_text(btn.label, name);
        lv_obj_set_style_bg_color(btn.btn, active ? ACCENT : TRACK, 0);
        lv_obj_set_style_text_color(btn.label, active ? TEXT : DIM, 0);
        showHidden(btn.btn, true);
    }
    for (int i = idx; i < MAX_DEV_BUTTONS; i++) {
        showHidden(devButtons[i].btn, false);
        devButtons[i].id[0] = '\0';
    }

    // 2. Master channel
    JsonObjectConst master = msg["master"].as<JsonObjectConst>();
    if (!master.isNull()) {
        int vol = master["vol"] | 0;
        bool muted = master["muted"] | false;
        if (!lv_obj_has_state(channels[0].slider, LV_STATE_PRESSED)) {
            lv_slider_set_value(channels[0].slider, vol, LV_ANIM_OFF);
            lv_label_set_text_fmt(channels[0].pctLabel, "%d%%", vol);
        }
        lv_obj_set_style_bg_color(channels[0].muteBtn, muted ? DANGER : TRACK, 0);
        lv_label_set_text(channels[0].muteLabel, muted ? LV_SYMBOL_MUTE " Mudo" : LV_SYMBOL_VOLUME_MAX " Mudo");
    }

    // 3. Media channel
    JsonObjectConst media = msg["media"].as<JsonObjectConst>();
    if (!media.isNull()) {
        const char* app = media["app"] | "";
        int vol = media["vol"] | 0;
        bool muted = media["muted"] | false;

        lv_label_set_text(channels[1].title, *app ? app : "Midia");
        if (!lv_obj_has_state(channels[1].slider, LV_STATE_PRESSED)) {
            lv_slider_set_value(channels[1].slider, vol, LV_ANIM_OFF);
            lv_label_set_text_fmt(channels[1].pctLabel, "%d%%", vol);
        }
        lv_obj_set_style_bg_color(channels[1].muteBtn, muted ? DANGER : TRACK, 0);
        lv_label_set_text(channels[1].muteLabel, muted ? LV_SYMBOL_MUTE " Mudo" : LV_SYMBOL_AUDIO " Mudo");
    }

    // 4. Mic channel
    JsonObjectConst mic = msg["mic"].as<JsonObjectConst>();
    if (!mic.isNull()) {
        int vol = mic["vol"] | 0;
        bool muted = mic["muted"] | false;
        if (!lv_obj_has_state(channels[2].slider, LV_STATE_PRESSED)) {
            lv_slider_set_value(channels[2].slider, vol, LV_ANIM_OFF);
            lv_label_set_text_fmt(channels[2].pctLabel, "%d%%", vol);
        }
        lv_obj_set_style_bg_color(channels[2].muteBtn, muted ? DANGER : TRACK, 0);
        lv_label_set_text(channels[2].muteLabel, muted ? "MIC Mudo" : "MIC Ligado");
    }
}

void audioSetLinked(bool linked) {
    for (int i = 0; i < 3; i++) {
        if (linked) {
            lv_obj_remove_state(channels[i].slider, LV_STATE_DISABLED);
            lv_obj_remove_state(channels[i].muteBtn, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(channels[i].slider, LV_STATE_DISABLED);
            lv_obj_add_state(channels[i].muteBtn, LV_STATE_DISABLED);
        }
    }
    for (int i = 0; i < MAX_DEV_BUTTONS; i++) {
        if (linked) lv_obj_remove_state(devButtons[i].btn, LV_STATE_DISABLED);
        else lv_obj_add_state(devButtons[i].btn, LV_STATE_DISABLED);
    }
}

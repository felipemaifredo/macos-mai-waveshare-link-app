// "Tocando": what Music or Spotify is playing, with the cover and media buttons.
//   {"t":"now","app","title","artist","album","playing","pos","dur"}  (app "" = nothing playing)
//   {"t":"art","jpg":"<base64 JPEG>"}                                   (no jpg = no cover)
#include "ui_common.h"

#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <string.h>

#include "src/libs/tjpgd/tjpgd.h"
#include "src/misc/cache/instance/lv_image_cache.h"

namespace {

constexpr int ART = 220;

lv_obj_t* artBox;
lv_obj_t* artImg;
lv_obj_t* artPlaceholder;
lv_obj_t* appLabel;
lv_obj_t* titleLabel;
lv_obj_t* artistLabel;
lv_obj_t* albumLabel;
lv_obj_t* progress;
lv_obj_t* posLabel;
lv_obj_t* durLabel;
lv_obj_t* playIcon;
lv_obj_t* buttons[3];

bool playing = false;
float pos = 0;
float dur = 0;
uint32_t posAt = 0;

lv_image_dsc_t artDsc[2];
uint16_t* artPixels[2] = {};
int artSlot = 0;

void fmtTime(lv_obj_t* l, float s) {
    const int t = s > 0 ? (int)s : 0;
    lv_label_set_text_fmt(l, "%d:%02d", t / 60, t % 60);
}

void updateProgress() {
    float p = pos;
    if (playing) p += (millis() - posAt) / 1000.0f;
    if (dur > 0 && p > dur) p = dur;
    lv_bar_set_value(progress, dur > 0 ? (int32_t)(p * 1000 / dur) : 0, LV_ANIM_OFF);
    fmtTime(posLabel, p);
    fmtTime(durLabel, dur);
}

void onTick(lv_timer_t*) {
    if (playing) updateProgress();
}

lv_obj_t* textLine(lv_obj_t* parent, const lv_font_t* font, lv_color_t color) {
    lv_obj_t* l = label(parent, font, color, "");
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return l;
}

// --- JPEG → RGB565 with tjpgd -----------------------------------------------

struct Jpeg {
    const uint8_t* data;
    size_t len;
    size_t at;
    uint16_t* out;
    int width;
};

size_t jpegInput(JDEC* jd, uint8_t* buf, size_t n) {
    Jpeg* j = static_cast<Jpeg*>(jd->device);
    if (n > j->len - j->at) n = j->len - j->at;
    if (buf) memcpy(buf, j->data + j->at, n);
    j->at += n;
    return n;
}

int jpegOutput(JDEC* jd, void* bitmap, JRECT* r) {
    Jpeg* j = static_cast<Jpeg*>(jd->device);
    const uint8_t* p = static_cast<const uint8_t*>(bitmap);
    for (int y = r->top; y <= r->bottom; y++) {
        for (int x = r->left; x <= r->right; x++) {
            const uint8_t b = *p++, g = *p++, rr = *p++; // tjpgd in LVGL writes B, G, R
            if (x < ART && y < ART) j->out[y * j->width + x] = ((rr & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
    }
    return 1;
}

bool decodeArt(const uint8_t* jpg, size_t len, uint16_t* out) {
    static uint8_t pool[4096];
    JDEC jd;
    Jpeg j = {jpg, len, 0, out, ART};
    if (jd_prepare(&jd, jpegInput, pool, sizeof(pool), &j) != JDR_OK) return false;
    if (jd.width != ART || jd.height != ART) return false;
    return jd_decomp(&jd, jpegOutput, 0) == JDR_OK;
}

void showArt(bool show) {
    showHidden(artImg, show);
    showHidden(artPlaceholder, !show);
}

} // namespace

void nowBuild(lv_obj_t* tile) {
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tile, 28, 0);
    lv_obj_set_style_pad_hor(tile, 14, 0);

    artBox = lv_obj_create(tile);
    lv_obj_remove_style_all(artBox);
    lv_obj_set_size(artBox, ART, ART);
    lv_obj_set_style_bg_color(artBox, CARD, 0);
    lv_obj_set_style_bg_opa(artBox, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(artBox, 16, 0);
    lv_obj_set_style_clip_corner(artBox, true, 0);
    lv_obj_remove_flag(artBox, LV_OBJ_FLAG_SCROLLABLE);
    artImg = lv_image_create(artBox);
    lv_obj_center(artImg);
    artPlaceholder = label(artBox, &lv_font_montserrat_48, DIM, LV_SYMBOL_AUDIO);
    lv_obj_center(artPlaceholder);
    showArt(false);

    lv_obj_t* col = box(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(col, 0);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_style_pad_row(col, 8, 0);

    appLabel = textLine(col, &lv_font_montserrat_16, DIM);
    titleLabel = textLine(col, &lv_font_montserrat_28, TEXT);
    artistLabel = textLine(col, &lv_font_montserrat_20, TEXT);
    albumLabel = textLine(col, &lv_font_montserrat_16, DIM);

    progress = lv_bar_create(col);
    lv_obj_set_size(progress, LV_PCT(100), 8);
    lv_obj_set_style_margin_top(progress, 14, 0);
    lv_bar_set_range(progress, 0, 1000);
    lv_obj_set_style_bg_color(progress, TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress, TEXT, LV_PART_INDICATOR);

    lv_obj_t* times = box(col, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(times, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    posLabel = label(times, &lv_font_montserrat_16, DIM, "0:00");
    durLabel = label(times, &lv_font_montserrat_16, DIM, "0:00");

    lv_obj_t* controls = box(col, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_margin_top(controls, 10, 0);
    lv_obj_set_style_pad_column(controls, 14, 0);
    const char* ids[] = {"key:prev", "key:play", "key:next"};
    const char* icons[] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_NEXT};
    for (int i = 0; i < 3; i++) {
        buttons[i] = deckButton(controls, ids[i], icons[i], nullptr);
        lv_obj_set_height(buttons[i], 96);
        lv_obj_set_flex_grow(buttons[i], 1);
    }
    playIcon = lv_obj_get_child(buttons[1], 0);

    lv_timer_create(onTick, 1000, nullptr);

    JsonDocument empty;
    nowShow(empty.as<JsonObjectConst>());
}

void nowShow(JsonObjectConst msg) {
    const char* app = msg["app"] | "";
    const bool any = *app;
    lv_label_set_text(appLabel, any ? app : "");
    lv_label_set_text(titleLabel, any ? (msg["title"] | "") : "Nada tocando");
    lv_obj_set_style_text_color(titleLabel, any ? TEXT : DIM, 0);
    lv_label_set_text(artistLabel, any ? (msg["artist"] | "") : "Abra o Music ou o Spotify");
    lv_label_set_text(albumLabel, any ? (msg["album"] | "") : "");

    playing = msg["playing"] | false;
    pos = msg["pos"] | 0.0f;
    dur = msg["dur"] | 0.0f;
    posAt = millis();
    lv_label_set_text(playIcon, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    updateProgress();
    if (!any) showArt(false);
}

void nowShowArt(const char* base64) {
    if (!base64 || !*base64) {
        showArt(false);
        return;
    }
    const size_t b64len = strlen(base64);
    size_t len = 0;
    uint8_t* jpg = static_cast<uint8_t*>(heap_caps_malloc(b64len * 3 / 4 + 4, MALLOC_CAP_SPIRAM));
    if (!jpg) return;
    const int slot = artSlot ^ 1;
    if (!artPixels[slot]) artPixels[slot] = static_cast<uint16_t*>(heap_caps_malloc(ART * ART * 2, MALLOC_CAP_SPIRAM));
    const bool ok = artPixels[slot] &&
                    mbedtls_base64_decode(jpg, b64len * 3 / 4 + 4, &len, (const uint8_t*)base64, b64len) == 0 &&
                    decodeArt(jpg, len, artPixels[slot]);
    heap_caps_free(jpg);
    if (!ok) {
        showArt(false);
        return;
    }

    lv_image_dsc_t& d = artDsc[slot];
    memset(&d, 0, sizeof(d));
    d.header.magic = LV_IMAGE_HEADER_MAGIC;
    d.header.cf = LV_COLOR_FORMAT_RGB565;
    d.header.w = ART;
    d.header.h = ART;
    d.header.stride = ART * 2;
    d.data_size = ART * ART * 2;
    d.data = reinterpret_cast<const uint8_t*>(artPixels[slot]);
    lv_image_cache_drop(&d);
    lv_image_set_src(artImg, &d);
    artSlot = slot;
    showArt(true);
}

void nowSetLinked(bool linked) {
    for (lv_obj_t* b : buttons) {
        if (linked) lv_obj_remove_state(b, LV_STATE_DISABLED);
        else lv_obj_add_state(b, LV_STATE_DISABLED);
    }
}

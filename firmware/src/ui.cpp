// Screen shell (header + pages), the usage page and the shortcuts page.
// The other pages live in ui_now.cpp, ui_mac.cpp, ui_docker.cpp and ui_clock.cpp.
#include "ui_common.h"

#include <string.h>

extern "C" const unsigned char montserrat_ttf[];
extern "C" const size_t montserrat_ttf_len;

namespace {

struct Page {
    const char* id;
    const char* name;
};
const Page PAGES[] = {
    {"usage", "Uso"}, {"now", "Tocando"}, {"keys", "Atalhos"},
    {"mac", "Mac"},   {"docker", "Docker"}, {"clock", "Hora"},
};
constexpr int PAGE_COUNT = sizeof(PAGES) / sizeof(PAGES[0]);
const char* PAGE_NAMES[] = {"Uso", "Tocando", "Atalhos", "Mac", "Docker", "Hora", ""};
constexpr lv_style_selector_t ITEM_CHECKED = (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_CHECKED;

struct UsageCard {
    lv_obj_t* name;
    lv_obj_t* pct;
    lv_obj_t* err;
    lv_obj_t* list;
    lv_obj_t* footer;
};

// Shortcut keys. The Mac receives "key:<id>" and decides what each one does (DeckActions in main.swift).
struct Key {
    const char* id;
    const char* icon;
    const char* text;
};
const Key KEYS[] = {
    {"shot", LV_SYMBOL_IMAGE, "Captura"},
    {"mic", "MIC", "Microfone"},
    {"sleep", LV_SYMBOL_EYE_CLOSE, "Apagar tela"},
    {"voldown", LV_SYMBOL_VOLUME_MID, "Volume -"},
    {"volup", LV_SYMBOL_VOLUME_MAX, "Volume +"},
    {"mute", LV_SYMBOL_MUTE, "Mudo"},
};
constexpr int KEY_COUNT = sizeof(KEYS) / sizeof(KEYS[0]);

UsageCard usageCards[2];
lv_obj_t* usbIcon;
lv_obj_t* micBadge;
lv_obj_t* camBadge;
lv_obj_t* refreshBtn;
lv_obj_t* pageSwitch;
lv_obj_t* pages;
lv_obj_t* tiles[PAGE_COUNT];
lv_obj_t* keyBtns[KEY_COUNT];
lv_obj_t* micBtn;
lv_obj_t* micCaption;
lv_timer_t* refreshTimeout = nullptr;
PressHandler pressHandler = nullptr;
PageHandler pageHandler = nullptr;
int currentPage = 0;
bool isLinked = false;

// --- Usage page -------------------------------------------------------------

void setRefreshing(bool on) {
    if (on || !isLinked) lv_obj_add_state(refreshBtn, LV_STATE_DISABLED);
    else lv_obj_remove_state(refreshBtn, LV_STATE_DISABLED);
}

void onRefreshTimeout(lv_timer_t*) {
    refreshTimeout = nullptr;
    setRefreshing(false);
}

void onRefresh(lv_event_t*) {
    sendPress("refresh");
    setRefreshing(true);
    if (refreshTimeout) lv_timer_delete(refreshTimeout);
    refreshTimeout = lv_timer_create(onRefreshTimeout, 20000, nullptr);
    lv_timer_set_repeat_count(refreshTimeout, 1);
}

UsageCard makeUsageCard(lv_obj_t* parent) {
    UsageCard c;
    lv_obj_t* o = card(parent);
    lv_obj_set_height(o, LV_PCT(100));
    lv_obj_set_flex_grow(o, 1);
    lv_obj_set_style_pad_row(o, 10, 0);

    lv_obj_t* head = box(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    c.name = label(head, &lv_font_montserrat_28, TEXT, "");
    c.pct = label(head, &lv_font_montserrat_48, TEXT, "--");

    c.err = label(o, &lv_font_montserrat_16, WARN, "");
    lv_obj_set_width(c.err, LV_PCT(100));
    lv_obj_add_flag(c.err, LV_OBJ_FLAG_HIDDEN);

    c.list = lv_obj_create(o);
    lv_obj_remove_style_all(c.list);
    lv_obj_set_width(c.list, LV_PCT(100));
    lv_obj_set_flex_grow(c.list, 1);
    lv_obj_set_flex_flow(c.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c.list, 14, 0);
    lv_obj_set_scroll_dir(c.list, LV_DIR_VER);
    label(c.list, &lv_font_montserrat_16, DIM, "Sem dados ainda");

    c.footer = label(o, &lv_font_montserrat_16, DIM, "");
    return c;
}

void addSection(lv_obj_t* list, const char* text) {
    lv_obj_t* l = label(list, &lv_font_montserrat_16, DIM, text);
    lv_obj_set_style_pad_top(l, 6, 0);
}

void addRow(lv_obj_t* list, const char* text, int pct, const char* reset, lv_color_t accent) {
    lv_obj_t* row = box(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, 6, 0);

    lv_obj_t* top = box(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    label(top, &lv_font_montserrat_20, TEXT, text);
    lv_obj_t* p = label(top, &lv_font_montserrat_20, levelColor(pct, TEXT), "");
    lv_label_set_text_fmt(p, "%d%%", pct);

    lv_obj_t* bar = lv_bar_create(row);
    lv_obj_set_size(bar, LV_PCT(100), 12);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, levelColor(pct, accent), LV_PART_INDICATOR);

    if (reset && *reset) {
        lv_obj_t* r = label(row, &lv_font_montserrat_16, DIM, "");
        lv_label_set_text_fmt(r, "reseta %s", reset);
    }
}

void fillUsageCard(UsageCard& c, JsonObjectConst p) {
    const char* name = p["name"] | "";
    const lv_color_t accent = strcmp(name, "Claude") == 0 ? CLAUDE : GEMINI;
    lv_label_set_text(c.name, name);
    lv_obj_set_style_text_color(c.name, accent, 0);

    const int pct = p["pct"] | -1;
    if (pct >= 0) lv_label_set_text_fmt(c.pct, "%d%%", pct);
    else lv_label_set_text(c.pct, "--");
    lv_obj_set_style_text_color(c.pct, levelColor(pct, TEXT), 0);

    const char* err = p["err"] | "";
    lv_label_set_text(c.err, err);
    showHidden(c.err, *err);

    const int32_t scroll = lv_obj_get_scroll_y(c.list);
    lv_obj_clean(c.list);
    const char* section = "";
    for (JsonObjectConst r : p["rows"].as<JsonArrayConst>()) {
        const char* sec = r["sec"] | "";
        if (*sec && strcmp(sec, section) != 0) addSection(c.list, sec);
        section = sec;
        addRow(c.list, r["label"] | "", r["pct"] | 0, r["reset"] | "", accent);
    }
    lv_obj_update_layout(c.list);
    lv_obj_scroll_to_y(c.list, scroll, LV_ANIM_OFF);

    const bool busy = p["busy"] | false;
    const char* at = p["at"] | "";
    if (busy) lv_label_set_text(c.footer, "Atualizando...");
    else if (*at) lv_label_set_text_fmt(c.footer, "Atualizado as %s", at);
    else lv_label_set_text(c.footer, "");
}

// --- Shortcuts page ---------------------------------------------------------

void makeDeck(lv_obj_t* parent) {
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(parent, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(parent, cols, rows);
    lv_obj_set_style_pad_column(parent, 14, 0);
    lv_obj_set_style_pad_row(parent, 14, 0);

    for (int i = 0; i < KEY_COUNT; i++) {
        char id[32];
        snprintf(id, sizeof(id), "key:%s", KEYS[i].id);
        lv_obj_t* b = deckButton(parent, id, KEYS[i].icon, KEYS[i].text);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
        keyBtns[i] = b;
        if (strcmp(KEYS[i].id, "mic") == 0) {
            micBtn = b;
            micCaption = lv_obj_get_child(b, 1);
        }
    }
}

// --- Shell --------------------------------------------------------------------

void pageShown(int idx) {
    lv_buttonmatrix_set_button_ctrl(pageSwitch, idx, LV_BUTTONMATRIX_CTRL_CHECKED);
    showHidden(refreshBtn, idx == 0);
    if (idx == currentPage) return;
    currentPage = idx;
    if (pageHandler) pageHandler(PAGES[idx].id);
}

void onPageSwitch(lv_event_t*) {
    const uint32_t idx = lv_buttonmatrix_get_selected_button(pageSwitch);
    if (idx >= PAGE_COUNT) return;
    lv_tileview_set_tile_by_index(pages, idx, 0, LV_ANIM_ON);
    pageShown(idx);
}

void onPageSwiped(lv_event_t*) {
    lv_obj_t* active = lv_tileview_get_tile_active(pages);
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (tiles[i] == active) pageShown(i);
    }
}

void onDeckButton(lv_event_t* e) {
    sendPress(static_cast<const char*>(lv_event_get_user_data(e)));
}

lv_obj_t* badge(lv_obj_t* parent, const char* text, lv_color_t color) {
    lv_obj_t* b = label(parent, &lv_font_montserrat_16, BG, text);
    lv_obj_set_style_bg_color(b, color, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_hor(b, 8, 0);
    lv_obj_set_style_pad_ver(b, 4, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    return b;
}

} // namespace

// --- Shared helpers (ui_common.h) ----------------------------------------------

lv_obj_t* box(lv_obj_t* parent, lv_flex_flow_t flow) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t* card(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_style_bg_color(o, CARD, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 16, 0);
    lv_obj_set_style_pad_all(o, 18, 0);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t* deckButton(lv_obj_t* parent, const char* id, const char* icon, const char* text) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_style_bg_color(b, CARD, 0);
    lv_obj_set_style_bg_color(b, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 14, 0);
    lv_obj_add_event_cb(b, onDeckButton, LV_EVENT_CLICKED, (void*)strdup(id));
    label(b, &lv_font_montserrat_48, TEXT, icon);
    if (text) label(b, &lv_font_montserrat_20, DIM, text);
    return b;
}

void sendPress(const char* id) {
    if (pressHandler && isLinked) pressHandler(id);
}

void showHidden(lv_obj_t* o, bool show) {
    if (show) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

lv_color_t levelColor(int pct, lv_color_t normal) {
    if (pct >= 90) return DANGER;
    if (pct >= 75) return WARN;
    return normal;
}

const lv_font_t* bigFont(int size) {
    static lv_font_t* cache[4] = {};
    static int sizes[4] = {};
    for (int i = 0; i < 4; i++) {
        if (sizes[i] == size) return cache[i];
        if (!sizes[i]) {
            cache[i] = lv_tiny_ttf_create_data(montserrat_ttf, montserrat_ttf_len, size);
            sizes[i] = size;
            return cache[i] ? cache[i] : &lv_font_montserrat_48;
        }
    }
    return &lv_font_montserrat_48;
}

// --- Public API (ui.h) ---------------------------------------------------------

void uiBegin(PressHandler onPress, PageHandler onPage) {
    pressHandler = onPress;
    pageHandler = onPage;

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, BG, 0);
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(scr, TEXT, 0);
    lv_obj_set_style_pad_all(scr, 14, 0);
    lv_obj_set_style_pad_row(scr, 12, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = box(scr, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(header, 52);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);

    pageSwitch = lv_buttonmatrix_create(header);
    lv_buttonmatrix_set_map(pageSwitch, PAGE_NAMES);
    lv_buttonmatrix_set_button_ctrl_all(pageSwitch, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(pageSwitch, true);
    lv_obj_set_size(pageSwitch, 570, 52);
    lv_obj_set_style_pad_all(pageSwitch, 4, 0);
    lv_obj_set_style_pad_column(pageSwitch, 4, 0);
    lv_obj_set_style_radius(pageSwitch, 12, 0);
    lv_obj_set_style_bg_color(pageSwitch, CARD, 0);
    lv_obj_set_style_border_width(pageSwitch, 0, 0);
    lv_obj_set_style_text_font(pageSwitch, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_radius(pageSwitch, 9, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(pageSwitch, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(pageSwitch, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_text_color(pageSwitch, DIM, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(pageSwitch, LV_OPA_COVER, ITEM_CHECKED);
    lv_obj_set_style_bg_color(pageSwitch, lv_color_hex(0x33363E), ITEM_CHECKED);
    lv_obj_set_style_text_color(pageSwitch, TEXT, ITEM_CHECKED);
    lv_obj_add_event_cb(pageSwitch, onPageSwitch, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* spacer = box(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(spacer, 0);
    lv_obj_set_flex_grow(spacer, 1);

    micBadge = badge(header, "MIC", WARN);
    camBadge = badge(header, LV_SYMBOL_VIDEO, GOOD);
    usbIcon = label(header, &lv_font_montserrat_20, DIM, LV_SYMBOL_USB);

    refreshBtn = lv_button_create(header);
    lv_obj_set_size(refreshBtn, 52, 52);
    lv_obj_set_style_radius(refreshBtn, 12, 0);
    lv_obj_set_style_shadow_width(refreshBtn, 0, 0);
    lv_obj_add_event_cb(refreshBtn, onRefresh, LV_EVENT_CLICKED, nullptr);
    lv_obj_center(label(refreshBtn, &lv_font_montserrat_20, TEXT, LV_SYMBOL_REFRESH));

    // The pages side by side: swipe or use the switch in the header.
    pages = lv_tileview_create(scr);
    lv_obj_set_width(pages, LV_PCT(100));
    lv_obj_set_flex_grow(pages, 1);
    lv_obj_set_style_bg_opa(pages, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(pages, onPageSwiped, LV_EVENT_VALUE_CHANGED, nullptr);
    for (int i = 0; i < PAGE_COUNT; i++) {
        const lv_dir_t dir = i == 0 ? LV_DIR_RIGHT : i == PAGE_COUNT - 1 ? LV_DIR_LEFT : LV_DIR_HOR;
        tiles[i] = lv_tileview_add_tile(pages, i, 0, dir);
        lv_obj_set_style_pad_all(tiles[i], 0, 0);
    }

    lv_obj_set_flex_flow(tiles[0], LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tiles[0], 14, 0);
    usageCards[0] = makeUsageCard(tiles[0]);
    usageCards[1] = makeUsageCard(tiles[0]);

    nowBuild(tiles[1]);
    makeDeck(tiles[2]);
    macBuild(tiles[3]);
    dockerBuild(tiles[4]);
    clockBuild(tiles[5]);

    lv_buttonmatrix_set_button_ctrl(pageSwitch, 0, LV_BUTTONMATRIX_CTRL_CHECKED);
    uiSetLinked(false);
}

const char* uiCurrentPage() { return PAGES[currentPage].id; }

void uiSetLinked(bool linked) {
    isLinked = linked;
    lv_obj_set_style_text_color(usbIcon, linked ? GOOD : DIM, 0);
    setRefreshing(refreshTimeout != nullptr);
    for (lv_obj_t* b : keyBtns) {
        if (linked) lv_obj_remove_state(b, LV_STATE_DISABLED);
        else lv_obj_add_state(b, LV_STATE_DISABLED);
    }
    nowSetLinked(linked);
    if (!linked) {
        lv_obj_add_flag(micBadge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(camBadge, LV_OBJ_FLAG_HIDDEN);
    }
}

void uiShowUsage(JsonArrayConst providers) {
    bool anyBusy = false;
    int i = 0;
    for (JsonObjectConst p : providers) {
        if (i >= 2) break;
        fillUsageCard(usageCards[i++], p);
        anyBusy |= p["busy"] | false;
    }
    if (!anyBusy && refreshTimeout) {
        lv_timer_delete(refreshTimeout);
        refreshTimeout = nullptr;
        setRefreshing(false);
    }
}

void uiShowMic(JsonObjectConst msg) {
    const bool muted = msg["muted"] | false;
    lv_obj_set_style_bg_color(micBtn, muted ? DANGER : CARD, 0);
    lv_obj_set_style_bg_color(micBtn, muted ? lv_color_hex(0xC23B3F) : PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(micCaption, muted ? TEXT : DIM, 0);
    lv_label_set_text(micCaption, muted ? "Microfone mudo" : "Microfone");
    showHidden(micBadge, msg["micInUse"] | false);
    showHidden(camBadge, msg["camInUse"] | false);
}

void uiShowNow(JsonObjectConst msg) { nowShow(msg); }
void uiShowArt(const char* base64) { nowShowArt(base64); }
void uiShowMac(JsonObjectConst msg) { macShow(msg); }
void uiShowDocker(JsonObjectConst msg) { dockerShow(msg); }
void uiShowClock(JsonObjectConst msg) { clockShow(msg); }

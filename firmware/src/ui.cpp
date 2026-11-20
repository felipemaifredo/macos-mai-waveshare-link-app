#include "ui.h"

#include <lvgl.h>
#include <string.h>

namespace {

const lv_color_t BG = lv_color_hex(0x101114);
const lv_color_t CARD = lv_color_hex(0x1B1D22);
const lv_color_t TEXT = lv_color_hex(0xE8E8EA);
const lv_color_t DIM = lv_color_hex(0x8A8F98);
const lv_color_t GOOD = lv_color_hex(0x3DBE78);
const lv_color_t WARN = lv_color_hex(0xF0A030);
const lv_color_t DANGER = lv_color_hex(0xE5484D);
const lv_color_t CLAUDE = lv_color_hex(0xD97757);
const lv_color_t GEMINI = lv_color_hex(0x4C8DF6);

struct Card {
    lv_obj_t* name;
    lv_obj_t* pct;
    lv_obj_t* err;
    lv_obj_t* list;
    lv_obj_t* footer;
};

// Deck keys. The Mac receives "key:<id>" and decides what each one does.
struct Key {
    const char* id;
    const char* icon;
    const char* text;
};
const Key KEYS[] = {
    {"shot", LV_SYMBOL_IMAGE, "Captura"},
    {"prev", LV_SYMBOL_PREV, "Anterior"},
    {"play", LV_SYMBOL_PLAY, "Play / Pausa"},
    {"next", LV_SYMBOL_NEXT, "Proxima"},
    {"voldown", LV_SYMBOL_VOLUME_MID, "Volume -"},
    {"volup", LV_SYMBOL_VOLUME_MAX, "Volume +"},
    {"mute", LV_SYMBOL_MUTE, "Mudo"},
    {"sleep", LV_SYMBOL_EYE_CLOSE, "Apagar tela"},
};
constexpr int KEY_COUNT = sizeof(KEYS) / sizeof(KEYS[0]);
const char* PAGE_NAMES[] = {"Uso", "Atalhos", ""};
constexpr lv_style_selector_t ITEM_CHECKED = (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_CHECKED;

Card cards[2];
lv_obj_t* statusLabel;
lv_obj_t* refreshBtn;
lv_obj_t* refreshLabel;
lv_obj_t* pageSwitch;
lv_obj_t* pages;
lv_obj_t* pageTiles[2];
lv_obj_t* keyBtns[KEY_COUNT];
lv_timer_t* refreshTimeout = nullptr;
PressHandler pressHandler = nullptr;

lv_color_t levelColor(int pct, lv_color_t normal) {
    if (pct >= 90) return DANGER;
    if (pct >= 75) return WARN;
    return normal;
}

// Invisible full-width container that sizes to its content.
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

void setRefreshing(bool on) {
    lv_label_set_text(refreshLabel, on ? LV_SYMBOL_REFRESH "  Atualizando..." : LV_SYMBOL_REFRESH "  Atualizar");
    if (on) lv_obj_add_state(refreshBtn, LV_STATE_DISABLED);
    else lv_obj_remove_state(refreshBtn, LV_STATE_DISABLED);
}

void onRefreshTimeout(lv_timer_t*) {
    refreshTimeout = nullptr;
    setRefreshing(false);
}

void onRefresh(lv_event_t*) {
    if (pressHandler) pressHandler("refresh");
    setRefreshing(true);
    if (refreshTimeout) lv_timer_delete(refreshTimeout);
    refreshTimeout = lv_timer_create(onRefreshTimeout, 20000, nullptr);
    lv_timer_set_repeat_count(refreshTimeout, 1);
}

Card makeCard(lv_obj_t* parent) {
    Card c;
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_height(card, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_bg_color(card, CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* head = box(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    c.name = label(head, &lv_font_montserrat_28, TEXT, "");
    c.pct = label(head, &lv_font_montserrat_48, TEXT, "--");

    c.err = label(card, &lv_font_montserrat_16, WARN, "");
    lv_obj_set_width(c.err, LV_PCT(100));
    lv_obj_add_flag(c.err, LV_OBJ_FLAG_HIDDEN);

    c.list = lv_obj_create(card);
    lv_obj_remove_style_all(c.list);
    lv_obj_set_width(c.list, LV_PCT(100));
    lv_obj_set_flex_grow(c.list, 1);
    lv_obj_set_flex_flow(c.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c.list, 14, 0);
    lv_obj_set_scroll_dir(c.list, LV_DIR_VER);
    label(c.list, &lv_font_montserrat_16, DIM, "Sem dados ainda");

    c.footer = label(card, &lv_font_montserrat_16, DIM, "");
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
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x2A2D34), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, levelColor(pct, accent), LV_PART_INDICATOR);

    if (reset && *reset) {
        lv_obj_t* r = label(row, &lv_font_montserrat_16, DIM, "");
        lv_label_set_text_fmt(r, "reseta %s", reset);
    }
}

void fillCard(Card& c, JsonObjectConst p) {
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
    if (*err) lv_obj_remove_flag(c.err, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(c.err, LV_OBJ_FLAG_HIDDEN);

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

// The refresh button only makes sense on the usage page.
void pageShown(int idx) {
    lv_buttonmatrix_set_button_ctrl(pageSwitch, idx, LV_BUTTONMATRIX_CTRL_CHECKED);
    if (idx == 0) lv_obj_remove_flag(refreshBtn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(refreshBtn, LV_OBJ_FLAG_HIDDEN);
}

void onPageSwitch(lv_event_t*) {
    const uint32_t idx = lv_buttonmatrix_get_selected_button(pageSwitch);
    if (idx > 1) return;
    lv_tileview_set_tile_by_index(pages, idx, 0, LV_ANIM_ON);
    pageShown(idx);
}

void onPageSwiped(lv_event_t*) {
    pageShown(lv_tileview_get_tile_active(pages) == pageTiles[1] ? 1 : 0);
}

void onKey(lv_event_t* e) {
    const Key* k = static_cast<const Key*>(lv_event_get_user_data(e));
    char id[32];
    snprintf(id, sizeof(id), "key:%s", k->id);
    if (pressHandler) pressHandler(id);
}

lv_obj_t* makeTile(int col) {
    lv_obj_t* t = lv_tileview_add_tile(pages, col, 0, col == 0 ? LV_DIR_RIGHT : LV_DIR_LEFT);
    lv_obj_set_style_pad_all(t, 0, 0);
    return t;
}

void makeDeck(lv_obj_t* parent) {
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(parent, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(parent, cols, rows);
    lv_obj_set_style_pad_column(parent, 14, 0);
    lv_obj_set_style_pad_row(parent, 14, 0);

    for (int i = 0; i < KEY_COUNT; i++) {
        lv_obj_t* b = lv_button_create(parent);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 4, 1, LV_GRID_ALIGN_STRETCH, i / 4, 1);
        lv_obj_set_style_bg_color(b, CARD, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2E3138), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(b, 14, 0);
        lv_obj_add_event_cb(b, onKey, LV_EVENT_CLICKED, const_cast<Key*>(&KEYS[i]));
        label(b, &lv_font_montserrat_48, TEXT, KEYS[i].icon);
        label(b, &lv_font_montserrat_20, DIM, KEYS[i].text);
        keyBtns[i] = b;
    }
}

} // namespace

void uiBegin(PressHandler onPress) {
    pressHandler = onPress;

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, BG, 0);
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(scr, TEXT, 0);
    lv_obj_set_style_pad_all(scr, 14, 0);
    lv_obj_set_style_pad_row(scr, 12, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = box(scr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 16, 0);

    pageSwitch = lv_buttonmatrix_create(header);
    lv_buttonmatrix_set_map(pageSwitch, PAGE_NAMES);
    lv_buttonmatrix_set_button_ctrl_all(pageSwitch, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(pageSwitch, true);
    lv_obj_set_size(pageSwitch, 300, 52);
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
    statusLabel = label(header, &lv_font_montserrat_16, DIM, "");

    refreshBtn = lv_button_create(header);
    lv_obj_set_size(refreshBtn, 220, 52);
    lv_obj_set_style_radius(refreshBtn, 12, 0);
    lv_obj_add_event_cb(refreshBtn, onRefresh, LV_EVENT_CLICKED, nullptr);
    refreshLabel = label(refreshBtn, &lv_font_montserrat_20, TEXT, "");
    lv_obj_center(refreshLabel);
    setRefreshing(false);

    // Two pages side by side: swipe or use the switch in the header.
    pages = lv_tileview_create(scr);
    lv_obj_set_width(pages, LV_PCT(100));
    lv_obj_set_flex_grow(pages, 1);
    lv_obj_set_style_bg_opa(pages, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(pages, onPageSwiped, LV_EVENT_VALUE_CHANGED, nullptr);

    pageTiles[0] = makeTile(0);
    lv_obj_set_flex_flow(pageTiles[0], LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pageTiles[0], 14, 0);
    cards[0] = makeCard(pageTiles[0]);
    cards[1] = makeCard(pageTiles[0]);

    pageTiles[1] = makeTile(1);
    makeDeck(pageTiles[1]);

    pageShown(0);
    uiSetLinked(false);
}

void uiSetLinked(bool linked) {
    lv_label_set_text(statusLabel, linked ? LV_SYMBOL_USB "  Mac conectado" : LV_SYMBOL_USB "  Aguardando o Mac...");
    lv_obj_set_style_text_color(statusLabel, linked ? GOOD : DIM, 0);
    if (linked) lv_obj_remove_state(refreshBtn, LV_STATE_DISABLED);
    else lv_obj_add_state(refreshBtn, LV_STATE_DISABLED);
    for (lv_obj_t* b : keyBtns) {
        if (linked) lv_obj_remove_state(b, LV_STATE_DISABLED);
        else lv_obj_add_state(b, LV_STATE_DISABLED);
    }
}

void uiShowUsage(JsonArrayConst providers) {
    bool anyBusy = false;
    int i = 0;
    for (JsonObjectConst p : providers) {
        if (i >= 2) break;
        fillCard(cards[i++], p);
        anyBusy |= p["busy"] | false;
    }
    if (!anyBusy && refreshTimeout) {
        lv_timer_delete(refreshTimeout);
        refreshTimeout = nullptr;
        setRefreshing(false);
    }
}

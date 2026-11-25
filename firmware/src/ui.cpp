// Screen shell (header + pages), the usage page and the shortcuts page.
// The other pages live in ui_now.cpp, ui_mac.cpp, ui_docker.cpp and ui_clock.cpp.
#include "ui_common.h"

#include <string.h>

#include "src/widgets/tileview/lv_tileview_private.h"

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
    {"audio", "Mixer"}, {"apps", "Apps"},   {"discord", "Discord"},
};
constexpr int PAGE_COUNT = sizeof(PAGES) / sizeof(PAGES[0]);
constexpr lv_style_selector_t ITEM_CHECKED = (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_CHECKED;

struct UsageCard {
    lv_obj_t* name;
    lv_obj_t* pct;
    lv_obj_t* err;
    lv_obj_t* list;
    lv_obj_t* footer;
};

// Icon names the Mac can use for shortcut keys (the same list is in Settings.swift).
struct Icon {
    const char* name;
    const char* symbol;
};
const Icon ICONS[] = {
    {"mic", "MIC"}, {"image", LV_SYMBOL_IMAGE}, {"audio", LV_SYMBOL_AUDIO}, {"video", LV_SYMBOL_VIDEO},
    {"play", LV_SYMBOL_PLAY}, {"pause", LV_SYMBOL_PAUSE}, {"prev", LV_SYMBOL_PREV}, {"next", LV_SYMBOL_NEXT},
    {"stop", LV_SYMBOL_STOP}, {"shuffle", LV_SYMBOL_SHUFFLE}, {"loop", LV_SYMBOL_LOOP},
    {"volume_mid", LV_SYMBOL_VOLUME_MID}, {"volume_max", LV_SYMBOL_VOLUME_MAX}, {"mute", LV_SYMBOL_MUTE},
    {"eye_close", LV_SYMBOL_EYE_CLOSE}, {"eye_open", LV_SYMBOL_EYE_OPEN}, {"home", LV_SYMBOL_HOME},
    {"settings", LV_SYMBOL_SETTINGS}, {"power", LV_SYMBOL_POWER}, {"refresh", LV_SYMBOL_REFRESH},
    {"bell", LV_SYMBOL_BELL}, {"envelope", LV_SYMBOL_ENVELOPE}, {"call", LV_SYMBOL_CALL},
    {"keyboard", LV_SYMBOL_KEYBOARD}, {"file", LV_SYMBOL_FILE}, {"directory", LV_SYMBOL_DIRECTORY},
    {"edit", LV_SYMBOL_EDIT}, {"save", LV_SYMBOL_SAVE}, {"trash", LV_SYMBOL_TRASH},
    {"download", LV_SYMBOL_DOWNLOAD}, {"upload", LV_SYMBOL_UPLOAD}, {"copy", LV_SYMBOL_COPY},
    {"paste", LV_SYMBOL_PASTE}, {"cut", LV_SYMBOL_CUT}, {"wifi", LV_SYMBOL_WIFI},
    {"bluetooth", LV_SYMBOL_BLUETOOTH}, {"gps", LV_SYMBOL_GPS}, {"charge", LV_SYMBOL_CHARGE},
    {"list", LV_SYMBOL_LIST}, {"plus", LV_SYMBOL_PLUS}, {"minus", LV_SYMBOL_MINUS}, {"ok", LV_SYMBOL_OK},
    {"close", LV_SYMBOL_CLOSE}, {"warning", LV_SYMBOL_WARNING}, {"drive", LV_SYMBOL_DRIVE},
    {"sd_card", LV_SYMBOL_SD_CARD}, {"usb", LV_SYMBOL_USB}, {"eject", LV_SYMBOL_EJECT}, {"tint", LV_SYMBOL_TINT},
};

const char* iconSymbol(const char* name) {
    for (const Icon& i : ICONS) {
        if (strcmp(i.name, name) == 0) return i.symbol;
    }
    return LV_SYMBOL_BULLET;
}

// Shortcut keys until the Mac sends its own list. The Mac receives "key:<id>" and decides what each does.
struct DefaultKey {
    const char* id;
    const char* icon;
    const char* text;
};
const DefaultKey DEFAULT_KEYS[] = {
    {"shot", "image", "Captura"},       {"mic", "mic", "Microfone"},       {"sleep", "eye_close", "Apagar tela"},
    {"voldown", "volume_mid", "Volume -"}, {"volup", "volume_max", "Volume +"}, {"mute", "mute", "Mudo"},
};
constexpr int MAX_KEYS = 12;

constexpr int USAGE_CARDS = 3; // Claude, Codex, Antigravity
UsageCard usageCards[USAGE_CARDS];
lv_obj_t* hostChip;
lv_obj_t* hostIcon;
lv_obj_t* hostName;
lv_obj_t* micBadge;
lv_obj_t* camBadge;
lv_obj_t* refreshBtn;
lv_obj_t* pageSwitch;
lv_obj_t* pages;
lv_obj_t* tiles[PAGE_COUNT];
const char* switchMap[PAGE_COUNT + 1];
int order[PAGE_COUNT]; // page indexes in tab order; only the first visibleCount are shown
int visibleCount = PAGE_COUNT;
lv_obj_t* keyBtns[MAX_KEYS];
int keyCount = 0;
lv_obj_t* appBtns[MAX_KEYS];
int appCount = 0;
lv_obj_t* micBtn = nullptr;
lv_obj_t* micCaption = nullptr;
lv_obj_t* discordDock = nullptr;
lv_obj_t* dockMuteBtn = nullptr;
lv_obj_t* dockMuteLabel = nullptr;
lv_obj_t* dockDeafBtn = nullptr;
lv_obj_t* dockDeafLabel = nullptr;
bool discordInCall = false;

lv_obj_t* musicDock = nullptr;
lv_obj_t* dockMusicApp = nullptr;
lv_obj_t* dockMusicTitle = nullptr;
lv_obj_t* dockMusicArtist = nullptr;
lv_obj_t* dockPlayBtn = nullptr;
lv_obj_t* dockPlayIcon = nullptr;
lv_obj_t* dockNextBtn = nullptr;
lv_obj_t* dockPrevBtn = nullptr;
bool musicPlaying = false;
bool musicActive = false;
bool micMuted = false;
char micText[48] = "Microfone"; // the mic key's own caption, shown when not muted
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

    // Name above the percentage: three cards side by side leave ~210 px each.
    lv_obj_t* head = box(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(head, 2, 0);
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
    const lv_color_t accent = strcmp(name, "Claude") == 0  ? CLAUDE
                              : strcmp(name, "Codex") == 0 ? CODEX
                                                           : GEMINI;
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

void applyMicState() {
    if (!micBtn) return;
    lv_obj_set_style_bg_color(micBtn, micMuted ? DANGER : CARD, 0);
    lv_obj_set_style_bg_color(micBtn, micMuted ? lv_color_hex(0xC23B3F) : PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(micCaption, micMuted ? TEXT : DIM, 0);
    lv_label_set_text(micCaption, micMuted ? "Microfone mudo" : micText);
}

void addKey(const char* id, const char* icon, const char* text) {
    if (keyCount >= MAX_KEYS) return;
    char press[48];
    snprintf(press, sizeof(press), "key:%s", id);
    lv_obj_t* b = deckButton(tiles[2], press, iconSymbol(icon), text);
    if (!isLinked) lv_obj_add_state(b, LV_STATE_DISABLED);
    if (strcmp(id, "mic") == 0) {
        micBtn = b;
        micCaption = lv_obj_get_child(b, 1);
        snprintf(micText, sizeof(micText), "%s", text);
    }
    keyBtns[keyCount++] = b;
}

// Lays the keys out in a grid that fits their number: up to 4 per row, 3 rows.
void layoutKeys() {
    static int32_t cols[5];
    static int32_t rows[4];
    const int n = keyCount > 0 ? keyCount : 1;
    const int ncol = n <= 4 ? n : n <= 6 ? 3 : 4;
    const int nrow = (n + ncol - 1) / ncol;
    for (int i = 0; i < ncol; i++) cols[i] = LV_GRID_FR(1);
    cols[ncol] = LV_GRID_TEMPLATE_LAST;
    for (int i = 0; i < nrow; i++) rows[i] = LV_GRID_FR(1);
    rows[nrow] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(tiles[2], cols, rows);
    for (int i = 0; i < keyCount; i++) {
        lv_obj_set_grid_cell(keyBtns[i], LV_GRID_ALIGN_STRETCH, i % ncol, 1, LV_GRID_ALIGN_STRETCH, i / ncol, 1);
    }
}

void clearKeys() {
    lv_obj_clean(tiles[2]);
    keyCount = 0;
    micBtn = nullptr;
    micCaption = nullptr;
}

void makeDeck() {
    lv_obj_set_layout(tiles[2], LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(tiles[2], 14, 0);
    lv_obj_set_style_pad_row(tiles[2], 14, 0);
    for (const DefaultKey& k : DEFAULT_KEYS) addKey(k.id, k.icon, k.text);
    layoutKeys();
}

// --- Apps page --------------------------------------------------------------

const DefaultKey DEFAULT_APPS[] = {
    {"term", "keyboard", "Terminal"}, {"safari", "wifi", "Safari"}, {"code", "edit", "VS Code"},
    {"discord", "call", "Discord"},   {"music", "audio", "Spotify"}, {"finder", "directory", "Finder"},
};

void addApp(const char* id, const char* icon, const char* text) {
    if (appCount >= MAX_KEYS) return;
    char press[48];
    snprintf(press, sizeof(press), "app:%s", id);
    lv_obj_t* b = deckButton(tiles[7], press, iconSymbol(icon), text);
    if (!isLinked) lv_obj_add_state(b, LV_STATE_DISABLED);
    appBtns[appCount++] = b;
}

void layoutApps() {
    static int32_t cols[5];
    static int32_t rows[4];
    const int n = appCount > 0 ? appCount : 1;
    const int ncol = n <= 4 ? n : n <= 6 ? 3 : 4;
    const int nrow = (n + ncol - 1) / ncol;
    for (int i = 0; i < ncol; i++) cols[i] = LV_GRID_FR(1);
    cols[ncol] = LV_GRID_TEMPLATE_LAST;
    for (int i = 0; i < nrow; i++) rows[i] = LV_GRID_FR(1);
    rows[nrow] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(tiles[7], cols, rows);
    for (int i = 0; i < appCount; i++) {
        lv_obj_set_grid_cell(appBtns[i], LV_GRID_ALIGN_STRETCH, i % ncol, 1, LV_GRID_ALIGN_STRETCH, i / ncol, 1);
    }
}

void clearApps() {
    lv_obj_clean(tiles[7]);
    appCount = 0;
}

void makeAppDeck() {
    lv_obj_set_layout(tiles[7], LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(tiles[7], 14, 0);
    lv_obj_set_style_pad_row(tiles[7], 14, 0);
    for (const DefaultKey& k : DEFAULT_APPS) addApp(k.id, k.icon, k.text);
    layoutApps();
}

// --- Shell --------------------------------------------------------------------

void updateDocks() {
    const bool isNowTab = (currentPage >= 0 && currentPage < PAGE_COUNT && strcmp(PAGES[currentPage].id, "now") == 0);

    if (discordInCall) {
        if (discordDock) showHidden(discordDock, true);
        if (musicDock) showHidden(musicDock, false);
    } else if (musicActive && !isNowTab) {
        if (discordDock) showHidden(discordDock, false);
        if (musicDock) showHidden(musicDock, true);
    } else {
        if (discordDock) showHidden(discordDock, false);
        if (musicDock) showHidden(musicDock, false);
    }
}

// `pos` is the tab position; order[pos] is the page.
void pageShown(int pos) {
    lv_buttonmatrix_set_button_ctrl(pageSwitch, pos, LV_BUTTONMATRIX_CTRL_CHECKED);
    const int page = order[pos];
    showHidden(refreshBtn, strcmp(PAGES[page].id, "usage") == 0);
    currentPage = page;
    if (pageHandler) pageHandler(PAGES[page].id);
    updateDocks();
}

void onPageSwitch(lv_event_t*) {
    const uint32_t pos = lv_buttonmatrix_get_selected_button(pageSwitch);
    if (pos >= (uint32_t)visibleCount) return;
    lv_tileview_set_tile_by_index(pages, pos, 0, LV_ANIM_ON);
    pageShown(pos);
}

void onPageSwiped(lv_event_t*) {
    lv_obj_t* active = lv_tileview_get_tile_active(pages);
    for (int pos = 0; pos < visibleCount; pos++) {
        if (tiles[order[pos]] == active) pageShown(pos);
    }
}

// Puts the visible tiles side by side in tab order and hides the others. The pages keep their state.
void applyPages() {
    bool shown[PAGE_COUNT] = {};
    for (int pos = 0; pos < visibleCount; pos++) {
        const int page = order[pos];
        shown[page] = true;
        switchMap[pos] = PAGES[page].name;
        lv_obj_remove_flag(tiles[page], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(tiles[page], lv_pct(pos * 100), 0);
        int dir = LV_DIR_NONE;
        if (pos > 0) dir |= LV_DIR_LEFT;
        if (pos < visibleCount - 1) dir |= LV_DIR_RIGHT;
        reinterpret_cast<lv_tileview_tile_t*>(tiles[page])->dir = static_cast<lv_dir_t>(dir);
    }
    switchMap[visibleCount] = "";
    for (int page = 0; page < PAGE_COUNT; page++) {
        if (shown[page]) continue;
        // The tileview finds the active tile by position and does not skip hidden ones,
        // so a hidden tile must never share a position with a visible one.
        lv_obj_add_flag(tiles[page], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(tiles[page], lv_pct((PAGE_COUNT + page) * 100), 0);
    }
    lv_buttonmatrix_set_map(pageSwitch, switchMap);
    lv_buttonmatrix_set_button_ctrl_all(pageSwitch, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_obj_set_width(pageSwitch, visibleCount > 6 ? (visibleCount > 7 ? visibleCount * 65 : visibleCount * 80) : visibleCount * 95);
    if (visibleCount > 7) {
        lv_obj_set_style_text_font(pageSwitch, &lv_font_montserrat_16, LV_PART_ITEMS);
    } else {
        lv_obj_set_style_text_font(pageSwitch, &lv_font_montserrat_20, LV_PART_ITEMS);
    }

    // Stay on the same page if it is still there, otherwise go to the first tab.
    int pos = 0;
    for (int i = 0; i < visibleCount; i++) {
        if (order[i] == currentPage) pos = i;
    }
    lv_obj_update_layout(pages);
    lv_tileview_set_tile_by_index(pages, pos, 0, LV_ANIM_OFF);
    pageShown(pos);
}

void onDeckButton(lv_event_t* e) {
    sendPress(static_cast<const char*>(lv_event_get_user_data(e)));
}

void onDeckButtonDeleted(lv_event_t* e) {
    free(lv_event_get_user_data(e));
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

// --- Hosts: the chip in the header, the list to switch hosts, the pairing code ----------

constexpr int MAX_UI_HOSTS = 6;
constexpr int MAX_UI_PAIRED = 4;
struct HostEntry {
    char name[24];
    bool wifi;
};
struct PairedEntry {
    char name[24];
    bool canWake;
};
HostEntry hostList[MAX_UI_HOSTS];
int hostCount = 0;
int hostActive = -1;
PairedEntry pairedList[MAX_UI_PAIRED];
int pairedCount = 0;
char netText[128] = "Wi-Fi: desligado";
HostSelectHandler hostSelectHandler = nullptr;
WakeHandler wakeHandler = nullptr;
VoidHandler pairCancelHandler = nullptr;
VoidHandler forgetHostsHandler = nullptr;
lv_obj_t* hostPopup = nullptr; // the backdrop on the top layer while the list is open
lv_obj_t* hostPopupList = nullptr;
lv_obj_t* hostPopupNet = nullptr;
lv_obj_t* forgetLabel = nullptr;
bool forgetArmed = false;
int wakeFeedback = 0; // -1: failed, 0: none, 1: sent
int wakeFeedbackIndex = -1;
lv_timer_t* wakeFeedbackTimer = nullptr;
lv_obj_t* pairOverlay = nullptr;
lv_obj_t* standby = nullptr; // full-screen clock while no computer is connected
lv_obj_t* standbyStatus = nullptr;

// Dims the screen; the dialogs sit on it. Deleted with lv_obj_delete_async because they
// close from their own buttons' callbacks.
lv_obj_t* backdrop() {
    lv_obj_t* b = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

lv_obj_t* dialog(lv_obj_t* parent, int width) {
    lv_obj_t* c = card(parent);
    lv_obj_set_size(c, width, LV_SIZE_CONTENT);
    lv_obj_center(c);
    lv_obj_set_style_pad_row(c, 12, 0);
    return c;
}

lv_obj_t* rowButton(lv_obj_t* parent, const char* icon, const char* text, lv_color_t bg, int height) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, LV_PCT(100), height);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_color(b, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(b, 16, 0);
    lv_obj_set_style_pad_column(b, 14, 0);
    label(b, &lv_font_montserrat_20, TEXT, icon);
    lv_obj_t* l = label(b, &lv_font_montserrat_20, TEXT, text);
    lv_obj_set_flex_grow(l, 1);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return b;
}

void refreshHostChip() {
    const bool on = hostActive >= 0 && hostActive < hostCount;
    lv_label_set_text(hostIcon, on && hostList[hostActive].wifi ? LV_SYMBOL_WIFI : LV_SYMBOL_USB);
    lv_obj_set_style_text_color(hostIcon, on ? GOOD : DIM, 0);
    lv_label_set_text(hostName, on ? hostList[hostActive].name : "Nenhum");
    lv_obj_set_style_text_color(hostName, on ? TEXT : DIM, 0);
}

void closeHostPopup() {
    if (hostPopup) lv_obj_delete_async(hostPopup);
    hostPopup = hostPopupList = hostPopupNet = forgetLabel = nullptr;
    forgetArmed = false;
}

void onHostPicked(lv_event_t* e) {
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    closeHostPopup();
    if (hostSelectHandler) hostSelectHandler(i);
}

void fillHostPopup();

void clearWakeFeedback(lv_timer_t*) {
    wakeFeedback = 0;
    wakeFeedbackIndex = -1;
    wakeFeedbackTimer = nullptr;
    fillHostPopup();
}

void onWakePicked(lv_event_t* e) {
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    wakeFeedbackIndex = index;
    wakeFeedback = wakeHandler && wakeHandler(index) ? 1 : -1;
    lv_obj_t* row = lv_event_get_target_obj(e);
    lv_obj_t* text = lv_obj_get_child(row, 1);
    lv_obj_t* status = text ? lv_obj_get_child(text, 1) : nullptr;
    if (status) lv_label_set_text(status, wakeFeedback > 0 ? "Sinal enviado..." : "Nao foi possivel enviar");
    if (wakeFeedbackTimer) lv_timer_delete(wakeFeedbackTimer);
    wakeFeedbackTimer = lv_timer_create(clearWakeFeedback, 3000, nullptr);
    lv_timer_set_repeat_count(wakeFeedbackTimer, 1);
}

lv_obj_t* pairedRow(lv_obj_t* parent, int index) {
    const PairedEntry& entry = pairedList[index];
    lv_obj_t* row = lv_button_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 62);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, TRACK, 0);
    lv_obj_set_style_bg_color(row, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_style_pad_column(row, 14, 0);
    label(row, &lv_font_montserrat_20, entry.canWake ? TEXT : DIM, LV_SYMBOL_POWER);

    lv_obj_t* text = box(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_style_pad_row(text, 2, 0);
    lv_obj_t* name = label(text, &lv_font_montserrat_16, DIM, entry.name);
    lv_obj_set_width(name, LV_PCT(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    const char* hint = entry.canWake ? "Toque para ligar" : "Conecte uma vez pelo Wi-Fi para habilitar";
    if (index == wakeFeedbackIndex) hint = wakeFeedback > 0 ? "Sinal enviado..." : "Nao foi possivel enviar";
    lv_obj_t* status = label(text, &lv_font_montserrat_16, entry.canWake ? TEXT : DIM, hint);
    lv_obj_set_width(status, LV_PCT(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_MODE_DOTS);
    if (entry.canWake) lv_obj_add_event_cb(row, onWakePicked, LV_EVENT_CLICKED, (void*)(intptr_t)index);
    else lv_obj_add_state(row, LV_STATE_DISABLED);
    return row;
}

void fillHostPopup() {
    if (!hostPopupList) return;
    lv_obj_clean(hostPopupList);
    if (hostCount == 0 && pairedCount == 0) {
        label(hostPopupList, &lv_font_montserrat_20, DIM, "Nenhum computador conectado");
    }
    for (int i = 0; i < hostCount; i++) {
        const bool on = i == hostActive;
        lv_obj_t* b = rowButton(hostPopupList, hostList[i].wifi ? LV_SYMBOL_WIFI : LV_SYMBOL_USB, hostList[i].name,
                                on ? lv_color_hex(0x33363E) : TRACK, 54);
        if (on) label(b, &lv_font_montserrat_20, GOOD, LV_SYMBOL_OK);
        lv_obj_add_event_cb(b, onHostPicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    if (pairedCount > 0) {
        lv_obj_t* heading = label(hostPopupList, &lv_font_montserrat_16, DIM, "Pareados (offline)");
        lv_obj_set_style_pad_top(heading, hostCount > 0 ? 8 : 0, 0);
        for (int i = 0; i < pairedCount; i++) pairedRow(hostPopupList, i);
    }
    lv_label_set_text(hostPopupNet, netText);
}

void onForgetHosts(lv_event_t*) {
    if (!forgetArmed) {
        forgetArmed = true;
        lv_label_set_text(forgetLabel, "Toque de novo para confirmar");
        return;
    }
    closeHostPopup();
    if (forgetHostsHandler) forgetHostsHandler();
}

void openHostPopup(lv_event_t*) {
    if (hostPopup || pairOverlay) return;
    hostPopup = backdrop();
    lv_obj_add_event_cb(hostPopup, [](lv_event_t* e) {
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) closeHostPopup();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* c = dialog(hostPopup, 480);
    lv_obj_set_height(c, 440);
    label(c, &lv_font_montserrat_28, TEXT, "Computadores");
    label(c, &lv_font_montserrat_16, DIM, "Escolha um conectado ou ligue um pareado.");
    hostPopupList = box(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(hostPopupList, 210);
    lv_obj_add_flag(hostPopupList, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(hostPopupList, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(hostPopupList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(hostPopupList, 8, 0);
    hostPopupNet = label(c, &lv_font_montserrat_16, DIM, "");
    lv_obj_set_width(hostPopupNet, LV_PCT(100));
    lv_obj_t* forget = rowButton(c, LV_SYMBOL_TRASH, "Esquecer pareamentos do Wi-Fi", CARD, 44);
    lv_obj_set_style_text_color(lv_obj_get_child(forget, 0), DIM, 0);
    forgetLabel = lv_obj_get_child(forget, 1);
    lv_obj_set_style_text_color(forgetLabel, DIM, 0);
    lv_obj_add_event_cb(forget, onForgetHosts, LV_EVENT_CLICKED, nullptr);
    fillHostPopup();
}

// The clock on its own (time over NTP, weather fetched by the board), over everything else while
// no computer is connected. Created before any dialog, so the dialogs (also on the top layer) stay
// above it. A tap opens the computer list (network state, pairings).
void makeStandby() {
    standby = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(standby);
    lv_obj_set_size(standby, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(standby, BG, 0);
    lv_obj_set_style_bg_opa(standby, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(standby, 24, 0);
    lv_obj_add_flag(standby, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(standby, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(standby, openHostPopup, LV_EVENT_CLICKED, nullptr);
    clockBuild(standby);
    standbyStatus = label(standby, &lv_font_montserrat_16, DIM, "");
    lv_obj_set_style_margin_top(standbyStatus, 28, 0);
}

void makeHostChip(lv_obj_t* header) {
    hostChip = lv_button_create(header);
    lv_obj_set_size(hostChip, 96, 52);
    lv_obj_set_style_radius(hostChip, 12, 0);
    lv_obj_set_style_shadow_width(hostChip, 0, 0);
    lv_obj_set_style_bg_color(hostChip, CARD, 0);
    lv_obj_set_style_bg_color(hostChip, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(hostChip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hostChip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(hostChip, 10, 0);
    lv_obj_set_style_pad_column(hostChip, 6, 0);
    hostIcon = label(hostChip, &lv_font_montserrat_20, DIM, LV_SYMBOL_USB);
    hostName = label(hostChip, &lv_font_montserrat_16, DIM, "");
    lv_obj_set_flex_grow(hostName, 1);
    lv_label_set_long_mode(hostName, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_add_event_cb(hostChip, openHostPopup, LV_EVENT_CLICKED, nullptr);
    refreshHostChip();
}

void showPairCode(const char* code, const char* name) {
    if (pairOverlay) lv_obj_delete_async(pairOverlay);
    pairOverlay = nullptr;
    if (!code) return;
    closeHostPopup();

    pairOverlay = backdrop();
    lv_obj_t* c = dialog(pairOverlay, 560);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(c, &lv_font_montserrat_28, TEXT, "Parear pelo Wi-Fi");
    lv_obj_t* who = label(c, &lv_font_montserrat_20, DIM, "");
    lv_label_set_text_fmt(who, "\"%s\" quer se conectar a esta tela.", name && *name ? name : "Um computador");
    label(c, &lv_font_montserrat_20, DIM, "Digite este codigo no computador:");
    char spaced[8];
    snprintf(spaced, sizeof(spaced), "%.3s %.3s", code, code + 3);
    lv_obj_t* digits = label(c, bigFont(88), ACCENT, spaced);
    lv_obj_set_style_pad_ver(digits, 6, 0);
    lv_obj_t* cancel = rowButton(c, LV_SYMBOL_CLOSE, "Cancelar", TRACK, 52);
    lv_obj_add_event_cb(cancel, [](lv_event_t*) {
        if (pairCancelHandler) pairCancelHandler();
    }, LV_EVENT_CLICKED, nullptr);
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
    char* copy = strdup(id);
    lv_obj_add_event_cb(b, onDeckButton, LV_EVENT_CLICKED, copy);
    lv_obj_add_event_cb(b, onDeckButtonDeleted, LV_EVENT_DELETE, copy);
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
    for (int i = 0; i < PAGE_COUNT; i++) {
        order[i] = i;
        switchMap[i] = PAGES[i].name;
    }
    switchMap[PAGE_COUNT] = "";
    lv_buttonmatrix_set_map(pageSwitch, switchMap);
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
    makeHostChip(header);

    refreshBtn = lv_button_create(header);
    lv_obj_set_size(refreshBtn, 52, 52);
    lv_obj_set_style_radius(refreshBtn, 12, 0);
    lv_obj_set_style_shadow_width(refreshBtn, 0, 0);
    lv_obj_add_event_cb(refreshBtn, onRefresh, LV_EVENT_CLICKED, nullptr);
    lv_obj_center(label(refreshBtn, &lv_font_montserrat_20, TEXT, LV_SYMBOL_REFRESH));

    // Content row: pages (left) + optional persistent Discord dock (right when in call)
    lv_obj_t* mainRow = box(scr, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(mainRow, LV_PCT(100));
    lv_obj_set_flex_grow(mainRow, 1);
    lv_obj_set_style_pad_column(mainRow, 10, 0);
    lv_obj_remove_flag(mainRow, LV_OBJ_FLAG_SCROLLABLE);

    // The pages side by side: swipe or use the switch in the header.
    pages = lv_tileview_create(mainRow);
    lv_obj_set_flex_grow(pages, 1);
    lv_obj_set_height(pages, LV_PCT(100));
    lv_obj_set_style_bg_opa(pages, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(pages, onPageSwiped, LV_EVENT_VALUE_CHANGED, nullptr);

    // Discord Sidebar Dock (visible when in call)
    discordDock = card(mainRow);
    lv_obj_set_size(discordDock, 110, LV_PCT(100));
    lv_obj_set_flex_flow(discordDock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(discordDock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(discordDock, 8, 0);
    lv_obj_set_style_pad_row(discordDock, 10, 0);
    lv_obj_set_style_border_color(discordDock, lv_color_hex(0x5865F2), 0);
    lv_obj_set_style_border_width(discordDock, 2, 0);
    lv_obj_add_flag(discordDock, LV_OBJ_FLAG_HIDDEN);

    label(discordDock, &lv_font_montserrat_16, lv_color_hex(0x5865F2), "DISCORD");

    dockMuteBtn = lv_button_create(discordDock);
    lv_obj_set_width(dockMuteBtn, LV_PCT(100));
    lv_obj_set_flex_grow(dockMuteBtn, 1);
    lv_obj_set_style_radius(dockMuteBtn, 14, 0);
    lv_obj_set_style_bg_color(dockMuteBtn, lv_color_hex(0x5865F2), 0);
    lv_obj_set_style_bg_color(dockMuteBtn, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(dockMuteBtn, 0, 0);
    lv_obj_set_flex_flow(dockMuteBtn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dockMuteBtn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(dockMuteBtn, [](lv_event_t*) { sendPress("discord:mute"); }, LV_EVENT_CLICKED, nullptr);
    label(dockMuteBtn, &lv_font_montserrat_28, TEXT, "MIC");
    dockMuteLabel = label(dockMuteBtn, &lv_font_montserrat_16, TEXT, "Mudo");

    dockDeafBtn = lv_button_create(discordDock);
    lv_obj_set_width(dockDeafBtn, LV_PCT(100));
    lv_obj_set_flex_grow(dockDeafBtn, 1);
    lv_obj_set_style_radius(dockDeafBtn, 14, 0);
    lv_obj_set_style_bg_color(dockDeafBtn, lv_color_hex(0x5865F2), 0);
    lv_obj_set_style_bg_color(dockDeafBtn, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(dockDeafBtn, 0, 0);
    lv_obj_set_flex_flow(dockDeafBtn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dockDeafBtn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(dockDeafBtn, [](lv_event_t*) { sendPress("discord:deaf"); }, LV_EVENT_CLICKED, nullptr);
    label(dockDeafBtn, &lv_font_montserrat_28, TEXT, LV_SYMBOL_AUDIO);
    dockDeafLabel = label(dockDeafBtn, &lv_font_montserrat_16, TEXT, "Ouvir");

    // Music Sidebar Dock (visible when music is active and not on the "now" tab)
    musicDock = card(mainRow);
    lv_obj_set_size(musicDock, 110, LV_PCT(100));
    lv_obj_set_flex_flow(musicDock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(musicDock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(musicDock, 8, 0);
    lv_obj_set_style_pad_row(musicDock, 6, 0);
    lv_obj_set_style_border_color(musicDock, lv_color_hex(0x1DB954), 0);
    lv_obj_set_style_border_width(musicDock, 2, 0);
    lv_obj_add_flag(musicDock, LV_OBJ_FLAG_HIDDEN);

    dockMusicApp = label(musicDock, &lv_font_montserrat_14, lv_color_hex(0x1DB954), "MUSICA");

    dockMusicTitle = label(musicDock, &lv_font_montserrat_14, TEXT, "Nada");
    lv_obj_set_width(dockMusicTitle, LV_PCT(100));
    lv_label_set_long_mode(dockMusicTitle, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(dockMusicTitle, LV_TEXT_ALIGN_CENTER, 0);

    dockMusicArtist = label(musicDock, &lv_font_montserrat_14, DIM, "");
    lv_obj_set_width(dockMusicArtist, LV_PCT(100));
    lv_label_set_long_mode(dockMusicArtist, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(dockMusicArtist, LV_TEXT_ALIGN_CENTER, 0);

    dockPlayBtn = lv_button_create(musicDock);
    lv_obj_set_width(dockPlayBtn, LV_PCT(100));
    lv_obj_set_flex_grow(dockPlayBtn, 1);
    lv_obj_set_style_radius(dockPlayBtn, 14, 0);
    lv_obj_set_style_bg_color(dockPlayBtn, lv_color_hex(0x1DB954), 0);
    lv_obj_set_style_bg_color(dockPlayBtn, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(dockPlayBtn, 0, 0);
    lv_obj_set_flex_flow(dockPlayBtn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dockPlayBtn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(dockPlayBtn, [](lv_event_t*) { sendPress("key:play"); }, LV_EVENT_CLICKED, nullptr);
    dockPlayIcon = label(dockPlayBtn, &lv_font_montserrat_28, TEXT, LV_SYMBOL_PLAY);

    dockNextBtn = lv_button_create(musicDock);
    lv_obj_set_size(dockNextBtn, LV_PCT(100), 54);
    lv_obj_set_style_radius(dockNextBtn, 12, 0);
    lv_obj_set_style_bg_color(dockNextBtn, TRACK, 0);
    lv_obj_set_style_bg_color(dockNextBtn, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(dockNextBtn, 0, 0);
    lv_obj_add_event_cb(dockNextBtn, [](lv_event_t*) { sendPress("key:next"); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_center(label(dockNextBtn, &lv_font_montserrat_20, TEXT, LV_SYMBOL_NEXT));

    dockPrevBtn = lv_button_create(musicDock);
    lv_obj_set_size(dockPrevBtn, LV_PCT(100), 48);
    lv_obj_set_style_radius(dockPrevBtn, 10, 0);
    lv_obj_set_style_bg_color(dockPrevBtn, TRACK, 0);
    lv_obj_set_style_bg_color(dockPrevBtn, PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(dockPrevBtn, 0, 0);
    lv_obj_add_event_cb(dockPrevBtn, [](lv_event_t*) { sendPress("key:prev"); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_center(label(dockPrevBtn, &lv_font_montserrat_16, DIM, LV_SYMBOL_PREV));

    for (int i = 0; i < PAGE_COUNT; i++) {
        tiles[i] = lv_tileview_add_tile(pages, i, 0, LV_DIR_HOR);
        lv_obj_set_style_pad_all(tiles[i], 0, 0);
    }

    lv_obj_set_flex_flow(tiles[0], LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tiles[0], 14, 0);
    for (UsageCard& c : usageCards) c = makeUsageCard(tiles[0]);

    nowBuild(tiles[1]);
    makeDeck();
    macBuild(tiles[3]);
    dockerBuild(tiles[4]);
    clockBuild(tiles[5]);
    audioBuild(tiles[6]);
    makeAppDeck();
    discordBuild(tiles[8]);

    currentPage = -1; // so applyPages reports the first page
    applyPages();
    makeStandby();
    uiSetLinked(false);
}

const char* uiCurrentPage() { return PAGES[currentPage].id; }

void uiSetLinked(bool linked) {
    isLinked = linked;
    setRefreshing(refreshTimeout != nullptr);
    for (int i = 0; i < keyCount; i++) {
        if (linked) lv_obj_remove_state(keyBtns[i], LV_STATE_DISABLED);
        else lv_obj_add_state(keyBtns[i], LV_STATE_DISABLED);
    }
    for (int i = 0; i < appCount; i++) {
        if (linked) lv_obj_remove_state(appBtns[i], LV_STATE_DISABLED);
        else lv_obj_add_state(appBtns[i], LV_STATE_DISABLED);
    }
    nowSetLinked(linked);
    audioSetLinked(linked);
    discordSetLinked(linked);
    if (discordDock) {
        if (linked) {
            lv_obj_remove_state(dockMuteBtn, LV_STATE_DISABLED);
            lv_obj_remove_state(dockDeafBtn, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(dockMuteBtn, LV_STATE_DISABLED);
            lv_obj_add_state(dockDeafBtn, LV_STATE_DISABLED);
        }
    }
    if (musicDock) {
        if (linked) {
            lv_obj_remove_state(dockPlayBtn, LV_STATE_DISABLED);
            lv_obj_remove_state(dockNextBtn, LV_STATE_DISABLED);
            lv_obj_remove_state(dockPrevBtn, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(dockPlayBtn, LV_STATE_DISABLED);
            lv_obj_add_state(dockNextBtn, LV_STATE_DISABLED);
            lv_obj_add_state(dockPrevBtn, LV_STATE_DISABLED);
        }
    }
    if (!linked) {
        // Nothing from the previous host may linger (also when the board switches hosts).
        lv_obj_add_flag(micBadge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(camBadge, LV_OBJ_FLAG_HIDDEN);
        discordInCall = false;
        musicActive = false;
        updateDocks();
    }
}

void uiShowUsage(JsonArrayConst providers) {
    bool anyBusy = false;
    int i = 0;
    for (JsonObjectConst p : providers) {
        if (i >= USAGE_CARDS) break;
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
    micMuted = msg["muted"] | false;
    applyMicState();
    showHidden(micBadge, msg["micInUse"] | false);
    showHidden(camBadge, msg["camInUse"] | false);
}

void uiShowNow(JsonObjectConst msg) {
    nowShow(msg);

    const char* app = msg["app"] | "";
    const char* title = msg["title"] | "";
    const char* artist = msg["artist"] | "";
    musicPlaying = msg["playing"] | false;
    musicActive = (*app != '\0') && (*title != '\0');

    if (musicDock) {
        lv_label_set_text(dockMusicApp, *app ? app : "MUSICA");
        lv_label_set_text(dockMusicTitle, *title ? title : "Sem musica");
        lv_label_set_text(dockMusicArtist, *artist ? artist : "");
        lv_label_set_text(dockPlayIcon, musicPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

        const bool isSpotify = (strcasecmp(app, "Spotify") == 0);
        const lv_color_t accent = isSpotify ? lv_color_hex(0x1DB954) : lv_color_hex(0xFC3C44);
        lv_obj_set_style_border_color(musicDock, accent, 0);
        lv_obj_set_style_text_color(dockMusicApp, accent, 0);
        lv_obj_set_style_bg_color(dockPlayBtn, musicPlaying ? accent : TRACK, 0);
    }

    updateDocks();
}
void uiShowArt(const char* base64) { nowShowArt(base64); }
void uiShowMac(JsonObjectConst msg) { macShow(msg); }
void uiShowDocker(JsonObjectConst msg) { dockerShow(msg); }
void uiShowClock(JsonObjectConst msg) { clockShow(msg); }
void uiShowAudio(JsonObjectConst msg) { audioShow(msg); }
void uiShowDiscord(JsonObjectConst msg) {
    discordInCall = msg["inCall"] | false;
    const bool isMuted = msg["muted"] | false;
    const bool isDeaf = msg["deafened"] | false;

    if (discordDock) {
        lv_obj_set_style_bg_color(dockMuteBtn, isMuted ? DANGER : lv_color_hex(0x5865F2), 0);
        lv_label_set_text(dockMuteLabel, isMuted ? "Mutado" : "Mic ON");

        lv_obj_set_style_bg_color(dockDeafBtn, isDeaf ? DANGER : lv_color_hex(0x5865F2), 0);
        lv_label_set_text(dockDeafLabel, isDeaf ? "Surdo" : "Ouvindo");
    }

    discordShow(msg);
    updateDocks();
}

void uiShowLayout(JsonObjectConst msg) {
    // Tabs: the ids in order. Unknown ids are skipped; an empty list keeps all of them.
    int n = 0;
    for (JsonVariantConst v : msg["pages"].as<JsonArrayConst>()) {
        const char* id = v | "";
        for (int page = 0; page < PAGE_COUNT; page++) {
            bool dup = false;
            for (int i = 0; i < n; i++) dup |= order[i] == page;
            if (!dup && strcmp(PAGES[page].id, id) == 0 && n < PAGE_COUNT) order[n++] = page;
        }
    }
    if (n > 0) {
        // Keep the hidden pages after the visible ones so every index stays valid.
        int all = n;
        for (int page = 0; page < PAGE_COUNT; page++) {
            bool used = false;
            for (int i = 0; i < n; i++) used |= order[i] == page;
            if (!used) order[all++] = page;
        }
        visibleCount = n;
        applyPages();
    }

    // Shortcut keys: [{"id","icon","text"}], up to 12.
    if (msg["keys"].is<JsonArrayConst>()) {
        clearKeys();
        for (JsonObjectConst k : msg["keys"].as<JsonArrayConst>()) {
            addKey(k["id"] | "", k["icon"] | "", k["text"] | "");
        }
        layoutKeys();
        applyMicState();
    }

    // App shortcut keys: [{"id","icon","text"}], up to 12.
    if (msg["apps"].is<JsonArrayConst>()) {
        clearApps();
        for (JsonObjectConst k : msg["apps"].as<JsonArrayConst>()) {
            addApp(k["id"] | "", k["icon"] | "", k["text"] | "");
        }
        layoutApps();
    }
}

void uiSetHostHandlers(HostSelectHandler onSelect, WakeHandler onWake, VoidHandler onPairCancel,
                       VoidHandler onForgetHosts) {
    hostSelectHandler = onSelect;
    wakeHandler = onWake;
    pairCancelHandler = onPairCancel;
    forgetHostsHandler = onForgetHosts;
}

void uiSetPaired(const UiPaired* paired, int count) {
    if (wakeFeedbackTimer) {
        lv_timer_delete(wakeFeedbackTimer);
        wakeFeedbackTimer = nullptr;
    }
    wakeFeedback = 0;
    wakeFeedbackIndex = -1;
    pairedCount = count < MAX_UI_PAIRED ? count : MAX_UI_PAIRED;
    for (int i = 0; i < pairedCount; i++) {
        snprintf(pairedList[i].name, sizeof(pairedList[i].name), "%s", paired[i].name);
        pairedList[i].canWake = paired[i].canWake;
    }
    fillHostPopup();
}

void uiSetHosts(const UiHost* hosts, int count, int active) {
    hostCount = count < MAX_UI_HOSTS ? count : MAX_UI_HOSTS;
    for (int i = 0; i < hostCount; i++) {
        snprintf(hostList[i].name, sizeof(hostList[i].name), "%s", hosts[i].name);
        hostList[i].wifi = hosts[i].wifi;
    }
    hostActive = active < hostCount ? active : -1;
    refreshHostChip();
    fillHostPopup();
}

void uiSetNet(const char* text) {
    snprintf(netText, sizeof(netText), "%s", text);
    if (hostPopupNet) lv_label_set_text(hostPopupNet, netText);
    if (standbyStatus) lv_label_set_text_fmt(standbyStatus, "Nenhum computador conectado   |   %s", netText);
}

void uiShowPairCode(const char* code, const char* hostName) { showPairCode(code, hostName); }

void uiSetStandby(bool on) {
    if (standby) showHidden(standby, on);
}

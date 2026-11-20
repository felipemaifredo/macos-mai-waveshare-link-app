// "Docker": the containers (view only). The Mac only polls while this page is visible.
//   {"t":"docker","up":bool,"containers":[{"name","image","state","status"}]}
#include "ui_common.h"

#include <string.h>

namespace {

lv_obj_t* summary;
lv_obj_t* list;

lv_color_t stateColor(const char* state) {
    if (strcmp(state, "running") == 0) return GOOD;
    if (strcmp(state, "restarting") == 0 || strcmp(state, "paused") == 0) return WARN;
    if (strcmp(state, "dead") == 0) return DANGER;
    return DIM;
}

void addContainer(JsonObjectConst c) {
    lv_obj_t* row = card(list);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 14, 0);

    const char* state = c["state"] | "";
    lv_obj_t* dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 14, 14);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, stateColor(state), 0);

    lv_obj_t* names = box(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(names, 0);
    lv_obj_set_flex_grow(names, 1);
    lv_obj_set_style_pad_row(names, 4, 0);
    lv_obj_t* n = label(names, &lv_font_montserrat_20, TEXT, c["name"] | "");
    lv_obj_set_width(n, LV_PCT(100));
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t* img = label(names, &lv_font_montserrat_16, DIM, c["image"] | "");
    lv_obj_set_width(img, LV_PCT(100));
    lv_label_set_long_mode(img, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t* st = label(row, &lv_font_montserrat_16, DIM, c["status"] | "");
    lv_obj_set_width(st, 240);
    lv_obj_set_style_text_align(st, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(st, LV_LABEL_LONG_MODE_DOTS);
}

} // namespace

void dockerBuild(lv_obj_t* tile) {
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 10, 0);
    summary = label(tile, &lv_font_montserrat_20, DIM, "Aguardando o Mac...");

    list = lv_obj_create(tile);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
}

void dockerShow(JsonObjectConst msg) {
    const int32_t scroll = lv_obj_get_scroll_y(list);
    lv_obj_clean(list);
    if (!(msg["up"] | false)) {
        lv_label_set_text(summary, LV_SYMBOL_WARNING "  Docker parado");
        lv_obj_set_style_text_color(summary, WARN, 0);
        return;
    }
    JsonArrayConst containers = msg["containers"].as<JsonArrayConst>();
    int running = 0;
    for (JsonObjectConst c : containers) {
        if (strcmp(c["state"] | "", "running") == 0) running++;
        addContainer(c);
    }
    if (containers.size() == 0) lv_label_set_text(summary, "Nenhum container");
    else lv_label_set_text_fmt(summary, "%d rodando de %d", running, (int)containers.size());
    lv_obj_set_style_text_color(summary, DIM, 0);
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, scroll, LV_ANIM_OFF);
}

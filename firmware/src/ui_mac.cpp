// "Mac": CPU, memory, battery and network. The Mac only sends these while this page is visible.
//   {"t":"mac","cpu","mem","memUsed","memTotal","bat","charging","ac","rx","tx"}
#include "ui_common.h"

namespace {

struct Stat {
    lv_obj_t* value;
    lv_obj_t* bar;
    lv_obj_t* detail;
};

Stat cpu, mem, bat;
lv_obj_t* cpuChart;
lv_chart_series_t* cpuSeries;
lv_obj_t* rxLabel;
lv_obj_t* txLabel;

lv_obj_t* statCard(lv_obj_t* parent, int col, int row, const char* title) {
    lv_obj_t* c = card(parent);
    lv_obj_set_grid_cell(c, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
    lv_obj_set_style_pad_row(c, 8, 0);
    label(c, &lv_font_montserrat_20, DIM, title);
    return c;
}

Stat makeStat(lv_obj_t* parent, int col, int row, const char* title) {
    Stat s;
    lv_obj_t* c = statCard(parent, col, row, title);
    s.value = label(c, &lv_font_montserrat_48, TEXT, "--");
    s.bar = lv_bar_create(c);
    lv_obj_set_size(s.bar, LV_PCT(100), 12);
    lv_bar_set_range(s.bar, 0, 100);
    lv_obj_set_style_bg_color(s.bar, TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s.bar, LV_OPA_COVER, LV_PART_MAIN);
    s.detail = label(c, &lv_font_montserrat_20, DIM, "");
    return s;
}

void setStat(Stat& s, int pct, lv_color_t color) {
    if (pct >= 0) lv_label_set_text_fmt(s.value, "%d%%", pct);
    else lv_label_set_text(s.value, "--");
    lv_bar_set_value(s.bar, pct > 0 ? pct : 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s.bar, color, LV_PART_INDICATOR);
}

void fmtRate(lv_obj_t* l, const char* icon, double bytesPerSec) {
    char num[16];
    const char* unit;
    if (bytesPerSec >= 1e6) {
        snprintf(num, sizeof(num), "%.1f", bytesPerSec / 1e6);
        unit = "MB/s";
    } else {
        snprintf(num, sizeof(num), "%.0f", bytesPerSec / 1e3);
        unit = "KB/s";
    }
    lv_label_set_text_fmt(l, "%s  %s %s", icon, num, unit);
}

} // namespace

void macBuild(lv_obj_t* tile) {
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(tile, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(tile, cols, rows);
    lv_obj_set_style_pad_column(tile, 14, 0);
    lv_obj_set_style_pad_row(tile, 14, 0);

    // CPU: value on the left, last 2 minutes on the right.
    lv_obj_t* c = statCard(tile, 0, 0, "CPU");
    lv_obj_t* row = box(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 14, 0);
    cpu.value = label(row, &lv_font_montserrat_48, TEXT, "--");
    lv_obj_set_width(cpu.value, 120);
    cpu.bar = nullptr;
    cpu.detail = nullptr;
    cpuChart = lv_chart_create(row);
    lv_obj_set_height(cpuChart, LV_PCT(100));
    lv_obj_set_flex_grow(cpuChart, 1);
    lv_chart_set_type(cpuChart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(cpuChart, 60);
    lv_chart_set_axis_range(cpuChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(cpuChart, 3, 0);
    lv_chart_set_update_mode(cpuChart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_opa(cpuChart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cpuChart, 0, 0);
    lv_obj_set_style_pad_all(cpuChart, 0, 0);
    lv_obj_set_style_line_color(cpuChart, TRACK, LV_PART_MAIN);
    lv_obj_set_style_size(cpuChart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(cpuChart, 3, LV_PART_ITEMS);
    cpuSeries = lv_chart_add_series(cpuChart, ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_values(cpuChart, cpuSeries, LV_CHART_POINT_NONE);

    mem = makeStat(tile, 1, 0, "Memoria");
    bat = makeStat(tile, 0, 1, "Bateria");

    lv_obj_t* net = statCard(tile, 1, 1, "Rede");
    lv_obj_set_flex_align(net, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    rxLabel = label(net, &lv_font_montserrat_28, TEXT, LV_SYMBOL_DOWNLOAD "  --");
    lv_obj_set_style_margin_top(rxLabel, 10, 0);
    txLabel = label(net, &lv_font_montserrat_28, TEXT, LV_SYMBOL_UPLOAD "  --");
    lv_obj_set_style_margin_top(txLabel, 10, 0);
}

void macShow(JsonObjectConst msg) {
    const int c = msg["cpu"] | -1;
    if (c >= 0) {
        lv_label_set_text_fmt(cpu.value, "%d%%", c);
        lv_obj_set_style_text_color(cpu.value, levelColor(c, TEXT), 0);
        lv_chart_set_next_value(cpuChart, cpuSeries, c);
    }

    const int m = msg["mem"] | -1;
    setStat(mem, m, levelColor(m, ACCENT));
    char gb[40]; // LVGL's own printf has no floats
    snprintf(gb, sizeof(gb), "%.1f de %.0f GB", msg["memUsed"] | 0.0, msg["memTotal"] | 0.0);
    lv_label_set_text(mem.detail, gb);

    const int b = msg["bat"] | -1;
    const bool charging = msg["charging"] | false;
    const bool ac = msg["ac"] | false;
    setStat(bat, b, b >= 0 && b <= 20 && !ac ? DANGER : GOOD);
    lv_label_set_text(bat.detail, b < 0 ? "Sem bateria" : charging ? LV_SYMBOL_CHARGE " Carregando" : ac ? "Na tomada" : "Na bateria");

    fmtRate(rxLabel, LV_SYMBOL_DOWNLOAD, msg["rx"] | 0.0);
    fmtRate(txLabel, LV_SYMBOL_UPLOAD, msg["tx"] | 0.0);
}

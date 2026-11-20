// "Hora": big clock, date and the weather in Cachoeiro de Itapemirim.
//   {"t":"clock","epoch":s,"tz":offset s,"city","temp","min","max","code"}  (weather fields optional)
// The board keeps time with millis() between messages.
#include "ui_common.h"

#include <time.h>

namespace {

const char* WEEKDAYS[] = {"Domingo", "Segunda", "Terca", "Quarta", "Quinta", "Sexta", "Sabado"};
const char* MONTHS[] = {"janeiro", "fevereiro", "marco", "abril", "maio", "junho",
                        "julho", "agosto", "setembro", "outubro", "novembro", "dezembro"};

lv_obj_t* timeLabel;
lv_obj_t* dateLabel;
lv_obj_t* weatherRow;
lv_obj_t* tempLabel;
lv_obj_t* skyLabel;
lv_obj_t* rangeLabel;

int64_t baseEpoch = 0; // local time (UTC + offset) at baseAt
uint32_t baseAt = 0;
int shownMinute = -1;

// WMO weather codes (Open-Meteo).
const char* sky(int code) {
    if (code == 0) return "Ceu limpo";
    if (code == 1) return "Poucas nuvens";
    if (code == 2) return "Parcialmente nublado";
    if (code == 3) return "Nublado";
    if (code == 45 || code == 48) return "Neblina";
    if (code >= 51 && code <= 57) return "Garoa";
    if (code == 61 || code == 66) return "Chuva fraca";
    if (code == 63) return "Chuva";
    if (code == 65 || code == 67) return "Chuva forte";
    if (code >= 71 && code <= 77) return "Neve";
    if (code >= 80 && code <= 82) return "Pancadas de chuva";
    if (code >= 95) return "Tempestade";
    return "";
}

void tick(lv_timer_t*) {
    if (!baseEpoch) return;
    const time_t now = baseEpoch + (millis() - baseAt) / 1000;
    struct tm t;
    gmtime_r(&now, &t);
    const int minute = t.tm_hour * 60 + t.tm_min;
    if (minute == shownMinute) return;
    shownMinute = minute;
    lv_label_set_text_fmt(timeLabel, "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text_fmt(dateLabel, "%s, %d de %s", WEEKDAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
}

} // namespace

void clockBuild(lv_obj_t* tile) {
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile, 6, 0);

    timeLabel = label(tile, bigFont(150), TEXT, "--:--");
    dateLabel = label(tile, &lv_font_montserrat_28, DIM, "");

    weatherRow = box(tile, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(weatherRow, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(weatherRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(weatherRow, 24, 0);
    lv_obj_set_style_margin_top(weatherRow, 24, 0);
    tempLabel = label(weatherRow, bigFont(64), TEXT, "");
    lv_obj_t* col = box(weatherRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(col, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(col, 4, 0);
    skyLabel = label(col, &lv_font_montserrat_28, TEXT, "");
    rangeLabel = label(col, &lv_font_montserrat_20, DIM, "");
    lv_obj_add_flag(weatherRow, LV_OBJ_FLAG_HIDDEN);

    lv_timer_create(tick, 1000, nullptr);
}

void clockShow(JsonObjectConst msg) {
    if (msg["epoch"].is<int64_t>()) {
        baseEpoch = msg["epoch"].as<int64_t>() + (msg["tz"] | 0);
        baseAt = millis();
        shownMinute = -1;
        tick(nullptr);
    }
    if (msg["temp"].is<float>()) {
        // ° is in the TTF but not in the built-in fonts, so it lives on the big label.
        lv_label_set_text_fmt(tempLabel, "%d\xC2\xB0", (int)lroundf(msg["temp"].as<float>()));
        lv_label_set_text(skyLabel, sky(msg["code"] | -1));
        lv_label_set_text_fmt(rangeLabel, "%s   Min %d   Max %d", msg["city"] | "",
                              (int)lroundf(msg["min"] | 0.0f), (int)lroundf(msg["max"] | 0.0f));
        lv_obj_remove_flag(weatherRow, LV_OBJ_FLAG_HIDDEN);
    }
}

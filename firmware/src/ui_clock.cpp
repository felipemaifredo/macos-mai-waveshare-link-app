// "Hora": big clock, date and the weather. Drawn twice: on the Hora page and on the standby
// screen shown while no computer is connected (ui.cpp).
//   {"t":"clock","epoch":s,"tz":offset s,"city","lat","lon","temp","min","max","code"}  (all optional)
// The time is the system clock: set by NTP once the board is on Wi-Fi (standalone.cpp), or by the
// host's "epoch". The weather comes from the board's own Open-Meteo fetch or from the host.
#include "ui_common.h"

#include <sys/time.h>
#include <time.h>

#include "standalone.h"

namespace {

const char* WEEKDAYS[] = {"Domingo", "Segunda", "Terca", "Quarta", "Quinta", "Sexta", "Sabado"};
const char* MONTHS[] = {"janeiro", "fevereiro", "marco", "abril", "maio", "junho",
                        "julho", "agosto", "setembro", "outubro", "novembro", "dezembro"};

struct View {
    lv_obj_t* time;
    lv_obj_t* date;
    lv_obj_t* weatherRow;
    lv_obj_t* temp;
    lv_obj_t* sky;
    lv_obj_t* range;
};
View views[2];
int viewCount = 0;
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

bool timeKnown() { return time(nullptr) > 1700000000; } // after 2023: set by NTP or a host

void tick(lv_timer_t*) {
    if (!timeKnown()) return;
    const time_t now = time(nullptr) + standaloneOffset();
    struct tm t;
    gmtime_r(&now, &t);
    const int minute = t.tm_hour * 60 + t.tm_min;
    if (minute == shownMinute) return;
    shownMinute = minute;
    for (int i = 0; i < viewCount; i++) {
        lv_label_set_text_fmt(views[i].time, "%02d:%02d", t.tm_hour, t.tm_min);
        lv_label_set_text_fmt(views[i].date, "%s, %d de %s", WEEKDAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
    }
}

} // namespace

void clockBuild(lv_obj_t* parent) {
    if (viewCount >= 2) return;
    View& v = views[viewCount++];
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 6, 0);

    v.time = label(parent, bigFont(150), TEXT, "--:--");
    v.date = label(parent, &lv_font_montserrat_28, DIM, "");

    v.weatherRow = box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(v.weatherRow, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(v.weatherRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(v.weatherRow, 24, 0);
    lv_obj_set_style_margin_top(v.weatherRow, 24, 0);
    v.temp = label(v.weatherRow, bigFont(64), TEXT, "");
    lv_obj_t* col = box(v.weatherRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(col, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(col, 4, 0);
    v.sky = label(col, &lv_font_montserrat_28, TEXT, "");
    v.range = label(col, &lv_font_montserrat_20, DIM, "");
    lv_obj_add_flag(v.weatherRow, LV_OBJ_FLAG_HIDDEN);

    if (viewCount == 1) lv_timer_create(tick, 1000, nullptr);
    shownMinute = -1;
    tick(nullptr);
}

void clockShowWeather(float temp, float min, float max, int code, const char* city) {
    for (int i = 0; i < viewCount; i++) {
        View& v = views[i];
        // ° is in the TTF but not in the built-in fonts, so it lives on the big label.
        lv_label_set_text_fmt(v.temp, "%d\xC2\xB0", (int)lroundf(temp));
        lv_label_set_text(v.sky, sky(code));
        lv_label_set_text_fmt(v.range, "%s   Min %d   Max %d", city, (int)lroundf(min), (int)lroundf(max));
        lv_obj_remove_flag(v.weatherRow, LV_OBJ_FLAG_HIDDEN);
    }
}

void clockShow(JsonObjectConst msg) {
    if (msg["tz"].is<int>()) standaloneSetOffset(msg["tz"].as<int>());
    if (msg["epoch"].is<int64_t>()) {
        // The host's clock is as good as NTP; it also covers boards without internet.
        struct timeval tv = {(time_t)msg["epoch"].as<int64_t>(), 0};
        settimeofday(&tv, nullptr);
    }
    if (msg["lat"].is<float>() && msg["lon"].is<float>()) {
        standaloneSetLocation(msg["lat"].as<float>(), msg["lon"].as<float>(), msg["city"] | "");
    }
    if (msg["temp"].is<float>()) {
        clockShowWeather(msg["temp"].as<float>(), msg["min"] | 0.0f, msg["max"] | 0.0f, msg["code"] | -1,
                         msg["city"] | "");
    }
    shownMinute = -1;
    tick(nullptr);
}

void clockRefresh() {
    shownMinute = -1;
    tick(nullptr);
}

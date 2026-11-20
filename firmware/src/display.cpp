#include "display.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

// Pins and timings follow Waveshare's ESP32-S3-Touch-LCD-7 demo.
namespace {

constexpr int W = 800;
constexpr int H = 480;

constexpr int PIN_SDA = 8;
constexpr int PIN_SCL = 9;
constexpr int PIN_TP_INT = 4;

// CH422G uses fixed I2C addresses per register instead of a device address.
constexpr uint8_t CH422G_MODE = 0x24;
constexpr uint8_t CH422G_OUT = 0x38;
constexpr uint8_t EXIO_TP_RST = 1 << 1;
constexpr uint8_t EXIO_LCD_BL = 1 << 2;
constexpr uint8_t EXIO_LCD_RST = 1 << 3;
// EXIO5 (USB_SEL) must stay LOW: HIGH routes GPIO19/20 to the CAN transceiver and the
// "USB" port disappears from the host.

uint8_t exioState = 0;
uint8_t gtAddr = 0;
esp_lcd_panel_handle_t panel = nullptr;

void exioWrite(uint8_t v) {
    exioState = v;
    Wire.beginTransmission(CH422G_OUT);
    Wire.write(v);
    Wire.endTransmission();
}

void resetChips() {
    Wire.beginTransmission(CH422G_MODE);
    Wire.write(0x01); // EXIO0-7 as push-pull outputs
    Wire.endTransmission();

    // LCD out of reset, backlight on, touch held in reset.
    exioWrite(EXIO_LCD_BL | EXIO_LCD_RST);
    // INT low while touch leaves reset selects GT911 address 0x5D.
    pinMode(PIN_TP_INT, OUTPUT);
    digitalWrite(PIN_TP_INT, LOW);
    delay(100);
    exioWrite(exioState | EXIO_TP_RST);
    delay(200);
    pinMode(PIN_TP_INT, INPUT);
}

// --- GT911 -----------------------------------------------------------------

bool gtRead(uint16_t reg, uint8_t* buf, size_t n) {
    Wire.beginTransmission(gtAddr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint16_t)gtAddr, n) != n) return false;
    for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

void gtWrite(uint16_t reg, uint8_t v) {
    Wire.beginTransmission(gtAddr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(v);
    Wire.endTransmission();
}

bool i2cPresent(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

bool touched = false;
int16_t touchX = 0, touchY = 0;
uint32_t lastPoll = 0;

void touchRead(lv_indev_t*, lv_indev_data_t* data) {
    if (gtAddr && millis() - lastPoll >= 10) {
        lastPoll = millis();
        uint8_t status;
        // Bit 7: new data ready. Low nibble: number of touch points.
        if (gtRead(0x814E, &status, 1) && (status & 0x80)) {
            uint8_t points = status & 0x0F;
            uint8_t p[4];
            if (points > 0 && gtRead(0x8150, p, 4)) {
                touchX = constrain(p[0] | (p[1] << 8), 0, W - 1);
                touchY = constrain(p[2] | (p[3] << 8), 0, H - 1);
                touched = true;
            } else {
                touched = false;
            }
            gtWrite(0x814E, 0);
        }
    }
    data->point.x = touchX;
    data->point.y = touchY;
    data->state = touched ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// --- RGB panel -------------------------------------------------------------

void panelBegin() {
    esp_lcd_rgb_panel_config_t c = {};
    c.clk_src = LCD_CLK_SRC_DEFAULT;
    c.timings.pclk_hz = 16 * 1000 * 1000;
    c.timings.h_res = W;
    c.timings.v_res = H;
    c.timings.hsync_pulse_width = 4;
    c.timings.hsync_back_porch = 8;
    c.timings.hsync_front_porch = 8;
    c.timings.vsync_pulse_width = 4;
    c.timings.vsync_back_porch = 8;
    c.timings.vsync_front_porch = 8;
    c.timings.flags.pclk_active_neg = 1;
    c.data_width = 16;
    c.bits_per_pixel = 16;
    c.num_fbs = 1;
    c.bounce_buffer_size_px = W * 10; // keeps the image stable while PSRAM is busy
    c.dma_burst_size = 64;
    c.hsync_gpio_num = 46;
    c.vsync_gpio_num = 3;
    c.de_gpio_num = 5;
    c.pclk_gpio_num = 7;
    c.disp_gpio_num = -1;
    const int data[16] = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40};
    for (int i = 0; i < 16; i++) c.data_gpio_nums[i] = data[i];
    c.flags.fb_in_psram = 1;

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&c, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
}

// --- LVGL glue -------------------------------------------------------------

uint32_t tick() { return millis(); }

void flush(lv_display_t* disp, const lv_area_t* a, uint8_t* px) {
    esp_lcd_panel_draw_bitmap(panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
    lv_display_flush_ready(disp);
}

} // namespace

void displayBegin() {
    Wire.begin(PIN_SDA, PIN_SCL, 400000);
    resetChips();
    for (uint8_t a : {0x5D, 0x14}) {
        if (i2cPresent(a)) { gtAddr = a; break; }
    }
    panelBegin();

    lv_init();
    lv_tick_set_cb(tick);

    lv_display_t* disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    const size_t bytes = W * 40 * 2;
    void* b1 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void* b2 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchRead);
}

bool touchFound() { return gtAddr != 0; }

void setBacklight(bool on) {
    exioWrite(on ? (exioState | EXIO_LCD_BL) : (exioState & ~EXIO_LCD_BL));
}

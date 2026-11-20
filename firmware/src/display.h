#pragma once

// Brings up the LCD, touch and LVGL. Call once from setup().
void displayBegin();

// True when the GT911 touch controller answered on I2C.
bool touchFound();

// Turns the backlight on or off (CH422G EXIO2).
void setBacklight(bool on);

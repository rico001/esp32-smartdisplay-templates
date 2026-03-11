#include <Arduino.h>
#include <esp32_smartdisplay.h>
#include "ui/ui.h"

auto lv_last_tick = millis();

void setup()
{
    Serial.begin(115200);
    smartdisplay_init();
    smartdisplay_lcd_set_backlight(1.0f);
    ui_init();
}

void loop()
{
    auto const now = millis();
    lv_tick_inc(now - lv_last_tick);
    lv_last_tick = now;
    lv_timer_handler();
}

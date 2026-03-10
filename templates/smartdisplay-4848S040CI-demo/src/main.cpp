#include <Arduino.h>
#include <esp32_smartdisplay.h>

auto lv_last_tick = millis();

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    auto const now = millis();
    lv_tick_inc(now - lv_last_tick);
    lv_last_tick = now;
    lv_timer_handler();
}

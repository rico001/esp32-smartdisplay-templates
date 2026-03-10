#include <Arduino.h>
#include <esp32_smartdisplay.h>

static lv_obj_t *tileview;
static int btn_count = 0;

static void btn_event_cb(lv_event_t *e)
{
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    btn_count++;
    lv_label_set_text_fmt(label, "Pressed %d times", btn_count);
}

static void create_screen_button(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Screen 1: Button");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click me!");
    lv_obj_center(btn_label);

    lv_obj_t *counter = lv_label_create(parent);
    lv_label_set_text(counter, "Pressed 0 times");
    lv_obj_align(counter, LV_ALIGN_CENTER, 0, 50);

    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, counter);
}

auto lv_last_tick = millis();

void setup()
{
    Serial.begin(115200);
    smartdisplay_init();

    tileview = lv_tileview_create(lv_screen_active());
    lv_obj_set_size(tileview, 480, 480);

    lv_obj_t *tile = lv_tileview_add_tile(tileview, 0, 0, (lv_dir_t)LV_DIR_RIGHT);
    create_screen_button(tile);
}

void loop()
{
    auto const now = millis();
    lv_tick_inc(now - lv_last_tick);
    lv_last_tick = now;
    lv_timer_handler();
}

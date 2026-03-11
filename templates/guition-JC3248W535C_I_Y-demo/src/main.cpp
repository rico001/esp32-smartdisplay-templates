#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>

// --- Pin Definitions ---
#define GFX_BL          1       // Backlight
#define TOUCH_SDA       4
#define TOUCH_SCL       8
#define TOUCH_RST       12
#define TOUCH_INT       11
#define TOUCH_ADDR      0x3B
#define TOUCH_I2C_CLOCK 400000

// --- Display Setup (Arduino_GFX) ---
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    45 /* CS */, 47 /* SCK */, 21 /* D0 */, 48 /* D1 */, 40 /* D2 */, 39 /* D3 */
);
Arduino_GFX *g = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, 320, 480);
Arduino_Canvas *gfx = new Arduino_Canvas(320, 480, g, 0, 0, 0);

// --- LVGL ---
#define SCREEN_W 480
#define SCREEN_H 320
#define LVGL_BUF_LINES 160  // Half screen height for larger partial updates
#define LVGL_BUF_SIZE (SCREEN_W * LVGL_BUF_LINES * 2)  // RGB565 = 2 bytes/pixel

static lv_display_t *lvgl_display;
static lv_indev_t *lvgl_touch_indev;
static uint8_t *draw_buf1;  // Allocated in PSRAM
static uint8_t *draw_buf2;  // Double buffer in PSRAM

// --- Touch reading ---
static bool touch_read(int16_t *x, int16_t *y)
{
    static const uint8_t read_cmd[11] = {
        0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00
    };

    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(read_cmd, sizeof(read_cmd));
    if (Wire.endTransmission() != 0) return false;

    if (Wire.requestFrom((uint16_t)TOUCH_ADDR, (uint8_t)8) < 8) return false;

    uint8_t buf[8];
    for (int i = 0; i < 8; i++) buf[i] = Wire.read();

    uint8_t touch_count = buf[1];

    if (touch_count == 0 || touch_count > 2) return false;

    *x = ((buf[2] & 0x0F) << 8) | buf[3];
    *y = ((buf[4] & 0x0F) << 8) | buf[5];

    return true;
}

// --- LVGL touch input callback ---
static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    int16_t tx, ty;
    if (touch_read(&tx, &ty)) {
        // Rotate touch 90° to match landscape (Canvas rotation=1)
        data->point.x = ty;
        data->point.y = 319 - tx;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// --- LVGL flush callback ---
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
    if (lv_display_flush_is_last(disp)) {
        gfx->flush();
    }
    lv_display_flush_ready(disp);
}

// --- Globals ---
static lv_obj_t *tileview;
static int btn_count = 0;

static void btn_event_cb(lv_event_t *e)
{
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    btn_count++;
    lv_label_set_text_fmt(label, "Pressed %d times", btn_count);
}

// Screen 1: Welcome & Button (title, button, counter centered)
static void create_screen_welcome(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "JC3248W535C");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *subtitle = lv_label_create(parent);
    lv_label_set_text(subtitle, "480x320 LVGL Demo");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x2196F3), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 65);

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 180, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click me!");
    lv_obj_center(btn_label);

    lv_obj_t *counter = lv_label_create(parent);
    lv_label_set_text(counter, "Pressed 0 times");
    lv_obj_align(counter, LV_ALIGN_CENTER, 0, 40);

    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, counter);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "swipe " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
}

// Screen 2: Keyboard (textarea top, full-width keyboard bottom)
static void create_screen_keyboard(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Keyboard");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 15, 8);

    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_placeholder_text(ta, "Type something...");
    lv_obj_set_size(ta, 450, 45);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 32);
    lv_textarea_set_one_line(ta, false);

    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, 480, 230);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, LV_SYMBOL_LEFT " swipe " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -10, 10);
}

// Screen 2: Widgets A (controls: slider, checkbox, dropdown, switch, arc, spinner)
static void create_screen_widgets_a(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Widgets A");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Left column
    lv_obj_t *slider_label = lv_label_create(parent);
    lv_label_set_text(slider_label, "Slider:");
    lv_obj_align(slider_label, LV_ALIGN_TOP_LEFT, 15, 40);

    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_width(slider, 150);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 85, 45);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);

    lv_obj_t *cb = lv_checkbox_create(parent);
    lv_checkbox_set_text(cb, "Enable something");
    lv_obj_align(cb, LV_ALIGN_TOP_LEFT, 15, 80);

    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, "Option A\nOption B\nOption C\nOption D");
    lv_obj_set_width(dd, 150);
    lv_obj_align(dd, LV_ALIGN_TOP_LEFT, 15, 115);

    lv_obj_t *sw_label = lv_label_create(parent);
    lv_label_set_text(sw_label, "Switch:");
    lv_obj_align(sw_label, LV_ALIGN_TOP_LEFT, 15, 165);

    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_align(sw, LV_ALIGN_TOP_LEFT, 90, 160);

    // Left bottom: progress bar
    lv_obj_t *bar_label = lv_label_create(parent);
    lv_label_set_text(bar_label, "Progress:");
    lv_obj_align(bar_label, LV_ALIGN_TOP_LEFT, 15, 210);

    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 140, 16);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 95, 212);
    lv_bar_set_value(bar, 65, LV_ANIM_OFF);

    // Right column: arc + spinner
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 110, 110);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 72);
    lv_obj_align(arc, LV_ALIGN_TOP_RIGHT, -60, 40);

    lv_obj_t *arc_val = lv_label_create(arc);
    lv_label_set_text(arc_val, "72%");
    lv_obj_center(arc_val);

    lv_obj_t *spinner = lv_spinner_create(parent);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_align(spinner, LV_ALIGN_TOP_RIGHT, -80, 170);
    lv_spinner_set_anim_params(spinner, 1000, 200);

    lv_obj_t *spin_label = lv_label_create(parent);
    lv_label_set_text(spin_label, "Loading...");
    lv_obj_set_style_text_font(spin_label, &lv_font_montserrat_12, 0);
    lv_obj_align(spin_label, LV_ALIGN_TOP_RIGHT, -70, 235);

    // Status LEDs bottom right
    lv_obj_t *led_label = lv_label_create(parent);
    lv_label_set_text(led_label, "Status:");
    lv_obj_align(led_label, LV_ALIGN_BOTTOM_LEFT, 15, -30);

    lv_obj_t *led_green = lv_led_create(parent);
    lv_obj_set_size(led_green, 22, 22);
    lv_obj_align(led_green, LV_ALIGN_BOTTOM_LEFT, 80, -28);
    lv_led_set_color(led_green, lv_color_hex(0x00FF00));
    lv_led_on(led_green);

    lv_obj_t *led_yellow = lv_led_create(parent);
    lv_obj_set_size(led_yellow, 22, 22);
    lv_obj_align(led_yellow, LV_ALIGN_BOTTOM_LEFT, 112, -28);
    lv_led_set_color(led_yellow, lv_color_hex(0xFFCC00));
    lv_led_set_brightness(led_yellow, 150);

    lv_obj_t *led_red = lv_led_create(parent);
    lv_obj_set_size(led_red, 22, 22);
    lv_obj_align(led_red, LV_ALIGN_BOTTOM_LEFT, 144, -28);
    lv_led_set_color(led_red, lv_color_hex(0xFF0000));
    lv_led_off(led_red);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, LV_SYMBOL_LEFT " swipe " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
}

static void msgbox_close_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    lv_msgbox_close(mbox);
}

static void show_msgbox_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *mbox = lv_msgbox_create(lv_screen_active());
    lv_msgbox_add_title(mbox, "Info");
    lv_msgbox_add_text(mbox, "This is a message box!\nShowing alerts works.");
    lv_obj_t *btn_ok = lv_msgbox_add_footer_button(mbox, "OK");
    lv_obj_add_event_cb(btn_ok, msgbox_close_cb, LV_EVENT_CLICKED, mbox);
    lv_obj_center(mbox);
}

// Screen 3: Widgets B (roller, spinbox, numpad)
static void create_screen_widgets_b(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Widgets B");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Left: roller
    lv_obj_t *roller = lv_roller_create(parent);
    lv_roller_set_options(roller,
        "January\nFebruary\nMarch\nApril\nMay\nJune\n"
        "July\nAugust\nSeptember\nOctober\nNovember\nDecember",
        LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 5);
    lv_obj_set_width(roller, 140);
    lv_obj_align(roller, LV_ALIGN_TOP_LEFT, 15, 40);

    // Left bottom: spinbox
    lv_obj_t *spb_label = lv_label_create(parent);
    lv_label_set_text(spb_label, "Value:");
    lv_obj_align(spb_label, LV_ALIGN_TOP_LEFT, 15, 210);

    lv_obj_t *spinbox = lv_spinbox_create(parent);
    lv_spinbox_set_range(spinbox, 0, 999);
    lv_spinbox_set_value(spinbox, 42);
    lv_spinbox_set_digit_format(spinbox, 3, 0);
    lv_obj_set_width(spinbox, 80);
    lv_obj_align(spinbox, LV_ALIGN_TOP_LEFT, 80, 205);

    // Msgbox button
    lv_obj_t *msgbox_btn = lv_button_create(parent);
    lv_obj_set_size(msgbox_btn, 140, 40);
    lv_obj_align(msgbox_btn, LV_ALIGN_BOTTOM_LEFT, 15, -30);
    lv_obj_set_style_bg_color(msgbox_btn, lv_color_hex(0xE91E63), 0);

    lv_obj_t *msgbox_lbl = lv_label_create(msgbox_btn);
    lv_label_set_text(msgbox_lbl, LV_SYMBOL_WARNING " Msgbox");
    lv_obj_center(msgbox_lbl);

    lv_obj_add_event_cb(msgbox_btn, show_msgbox_cb, LV_EVENT_CLICKED, NULL);

    // Right: numpad
    static const char *btnm_map[] = {"1", "2", "3", "\n",
                                      "4", "5", "6", "\n",
                                      "7", "8", "9", "\n",
                                      "*", "0", "#", ""};
    lv_obj_t *btnm = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(btnm, btnm_map);
    lv_obj_set_size(btnm, 200, 250);
    lv_obj_align(btnm, LV_ALIGN_TOP_RIGHT, -15, 40);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, LV_SYMBOL_LEFT " swipe " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
}

// Screen 4: Charts & Data (landscape: chart top-wide, scale+table side-by-side below)
static void create_screen_charts(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Charts & Data");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    // Chart spanning full width
    lv_obj_t *chart = lv_chart_create(parent);
    lv_obj_set_size(chart, 440, 110);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 28);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 10);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);

    lv_chart_series_t *ser1 = lv_chart_add_series(chart, lv_color_hex(0x2196F3), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_series_t *ser2 = lv_chart_add_series(chart, lv_color_hex(0xFF5722), LV_CHART_AXIS_PRIMARY_Y);

    static const int32_t data1[] = {10, 25, 40, 35, 60, 75, 50, 65, 80, 90};
    static const int32_t data2[] = {50, 45, 30, 55, 40, 20, 35, 25, 15, 10};
    for (int i = 0; i < 10; i++) {
        lv_chart_set_next_value(chart, ser1, data1[i]);
        lv_chart_set_next_value(chart, ser2, data2[i]);
    }

    lv_obj_t *chart_label = lv_label_create(parent);
    lv_label_set_text(chart_label, "Temp (blue) / Humidity (red)");
    lv_obj_set_style_text_font(chart_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(chart_label, chart, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // Bottom left: scale gauge
    lv_obj_t *scale = lv_scale_create(parent);
    lv_obj_set_size(scale, 100, 100);
    lv_obj_align(scale, LV_ALIGN_BOTTOM_LEFT, 20, -30);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_OUTER);
    lv_scale_set_range(scale, 0, 100);
    lv_scale_set_total_tick_count(scale, 21);
    lv_scale_set_major_tick_every(scale, 5);
    lv_scale_set_label_show(scale, true);

    lv_obj_t *scale_label = lv_label_create(parent);
    lv_label_set_text(scale_label, "RPM");
    lv_obj_set_style_text_font(scale_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(scale_label, scale, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // Bottom right: table
    lv_obj_t *table = lv_table_create(parent);
    lv_table_set_column_count(table, 3);
    lv_table_set_column_width(table, 0, 90);
    lv_table_set_column_width(table, 1, 80);
    lv_table_set_column_width(table, 2, 60);

    lv_table_set_cell_value(table, 0, 0, "Sensor");
    lv_table_set_cell_value(table, 0, 1, "Value");
    lv_table_set_cell_value(table, 0, 2, "Status");
    lv_table_set_cell_value(table, 1, 0, "Temp");
    lv_table_set_cell_value(table, 1, 1, "23.5 C");
    lv_table_set_cell_value(table, 1, 2, "OK");
    lv_table_set_cell_value(table, 2, 0, "Humidity");
    lv_table_set_cell_value(table, 2, 1, "58%");
    lv_table_set_cell_value(table, 2, 2, "OK");
    lv_table_set_cell_value(table, 3, 0, "Pressure");
    lv_table_set_cell_value(table, 3, 1, "1013 hPa");
    lv_table_set_cell_value(table, 3, 2, "WARN");

    lv_obj_set_size(table, 250, 150);
    lv_obj_align(table, LV_ALIGN_BOTTOM_RIGHT, -10, -20);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, LV_SYMBOL_LEFT " swipe " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -3);
}

// Screen 5: Calendar & List (landscape: calendar left, list right)
static void create_screen_callist(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Calendar & List");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    // Left: calendar
    lv_obj_t *cal = lv_calendar_create(parent);
    lv_obj_set_size(cal, 250, 270);
    lv_obj_align(cal, LV_ALIGN_TOP_LEFT, 10, 28);
    lv_calendar_set_today_date(cal, 2026, 3, 11);
    lv_calendar_set_showed_date(cal, 2026, 3);

    static lv_calendar_date_t highlighted[] = {
        {2026, 3, 11},
        {2026, 3, 15},
        {2026, 3, 20},
    };
    lv_calendar_set_highlighted_dates(cal, highlighted, 3);

    // Right: list
    lv_obj_t *list = lv_list_create(parent);
    lv_obj_set_size(list, 190, 270);
    lv_obj_align(list, LV_ALIGN_TOP_RIGHT, -10, 28);

    lv_list_add_text(list, "Settings");
    lv_list_add_button(list, LV_SYMBOL_WIFI, "WiFi");
    lv_list_add_button(list, LV_SYMBOL_BLUETOOTH, "Bluetooth");
    lv_list_add_button(list, LV_SYMBOL_GPS, "GPS");
    lv_list_add_text(list, "System");
    lv_list_add_button(list, LV_SYMBOL_SETTINGS, "Config");
    lv_list_add_button(list, LV_SYMBOL_POWER, "Power");
    lv_list_add_button(list, LV_SYMBOL_HOME, "Home");

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, LV_SYMBOL_LEFT " swipe");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -3);
}

// --- Arduino Setup & Loop ---
auto lv_last_tick = millis();

void setup()
{
    Serial.begin(115200);
    Serial.println("JC3248W535C starting...");

    // Init display
    gfx->begin();
    gfx->fillScreen(BLACK);

    // Rotate to landscape
    gfx->setRotation(1);

    // Backlight on
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    // Init touch (I2C)
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(TOUCH_I2C_CLOCK);
    pinMode(TOUCH_INT, INPUT_PULLUP);
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(200);
    digitalWrite(TOUCH_RST, HIGH);
    delay(200);

    Serial.println("Display + Touch initialized");

    // Init LVGL
    lv_init();

    // Allocate draw buffers in PSRAM (larger + double buffered)
    draw_buf1 = (uint8_t *)ps_malloc(LVGL_BUF_SIZE);
    draw_buf2 = (uint8_t *)ps_malloc(LVGL_BUF_SIZE);
    Serial.printf("Draw buffers: %d bytes each in PSRAM (double buffered)\n", LVGL_BUF_SIZE);

    // Create display with double buffering
    lvgl_display = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_buffers(lvgl_display, draw_buf1, draw_buf2, LVGL_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(lvgl_display, lvgl_flush_cb);
    // Rotation handled by Arduino_GFX Canvas, not LVGL

    // Create touch input device
    lvgl_touch_indev = lv_indev_create();
    lv_indev_set_type(lvgl_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_touch_indev, lvgl_touch_cb);

    // Create tileview with 6 horizontal screens (swipe left/right)
    tileview = lv_tileview_create(lv_screen_active());
    lv_obj_set_size(tileview, SCREEN_W, SCREEN_H);

    // Reduce swipe sensitivity: require longer drag distance before scrolling
    lv_obj_set_scroll_snap_x(tileview, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_snap_y(tileview, LV_SCROLL_SNAP_NONE);
    lv_indev_set_scroll_limit(lvgl_touch_indev, 50);
    lv_indev_set_scroll_throw(lvgl_touch_indev, 5);

    lv_obj_t *tile1 = lv_tileview_add_tile(tileview, 0, 0, (lv_dir_t)LV_DIR_RIGHT);
    lv_obj_t *tile2 = lv_tileview_add_tile(tileview, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    lv_obj_t *tile3 = lv_tileview_add_tile(tileview, 2, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    lv_obj_t *tile4 = lv_tileview_add_tile(tileview, 3, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    lv_obj_t *tile5 = lv_tileview_add_tile(tileview, 4, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    lv_obj_t *tile6 = lv_tileview_add_tile(tileview, 5, 0, (lv_dir_t)LV_DIR_LEFT);

    create_screen_welcome(tile1);
    create_screen_keyboard(tile2);
    create_screen_widgets_a(tile3);
    create_screen_widgets_b(tile4);
    create_screen_charts(tile5);
    create_screen_callist(tile6);

    Serial.println("UI ready");
}

void loop()
{
    auto const now = millis();
    lv_tick_inc(now - lv_last_tick);
    lv_last_tick = now;
    lv_timer_handler();
}

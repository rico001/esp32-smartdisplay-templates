#include "image_screen_from_inet.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <lvgl.h>

static const char *IMAGE_URL =
    "https://images.tagesschau.de/image/ae595fe8-9b20-4538-b756-b233ada9160f/"
    "AAABlgXa1g0/AAABmyZFCYU/1x1-small/"
    "gliese-supersonne-exoplanet-101.jpg?width=256";

// Persistent state
static lv_obj_t *status_label = NULL;
static lv_obj_t *img_widget   = NULL;
static lv_obj_t *load_btn     = NULL;
static uint8_t  *jpeg_buf     = NULL;
static uint32_t  jpeg_len     = 0;
static lv_image_dsc_t img_dsc;

// JPEG SOF0 Marker parsen um Breite/Hoehe zu ermitteln
static bool jpeg_get_dimensions(const uint8_t *data, uint32_t len, uint16_t *w, uint16_t *h)
{
    uint32_t i = 2; // Skip FF D8
    while (i + 4 < len) {
        if (data[i] != 0xFF) return false;
        uint8_t marker = data[i + 1];
        uint16_t seg_len = (data[i + 2] << 8) | data[i + 3];

        // SOF0..SOF3 enthalten die Dimensionen
        if (marker >= 0xC0 && marker <= 0xC3) {
            if (i + 9 > len) return false;
            *h = (data[i + 5] << 8) | data[i + 6];
            *w = (data[i + 7] << 8) | data[i + 8];
            return true;
        }
        i += 2 + seg_len;
    }
    return false;
}

static void do_download(lv_event_t *e)
{
    (void)e;

    if (WiFi.status() != WL_CONNECTED) {
        lv_label_set_text(status_label, LV_SYMBOL_WARNING "  Kein WLAN!");
        return;
    }

    Serial.printf("[IMG] Free heap: %u, free PSRAM: %u\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());

    lv_label_set_text(status_label, "Lade Bild...");
    lv_obj_add_state(load_btn, LV_STATE_DISABLED);
    lv_refr_now(NULL); // UI sofort updaten bevor wir blockieren

    WiFiClientSecure client;
    client.setInsecure();                // Kein Zertifikat pruefen (spart RAM)

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    http.begin(client, IMAGE_URL);

    Serial.println("[IMG] Starting HTTP GET...");
    int code = http.GET();
    Serial.printf("[IMG] HTTP response: %d\n", code);
    if (code != HTTP_CODE_OK) {
        Serial.printf("[IMG] HTTP error: %s\n", http.errorToString(code).c_str());
        lv_label_set_text_fmt(status_label, LV_SYMBOL_WARNING "  HTTP %d", code);
        http.end();
        lv_obj_remove_state(load_btn, LV_STATE_DISABLED);
        return;
    }

    int content_len = http.getSize();
    if (content_len <= 0) {
        // Chunked — lese alles in einen String-Buffer
        String payload = http.getString();
        content_len = payload.length();
        if (content_len <= 0) {
            lv_label_set_text(status_label, LV_SYMBOL_WARNING "  Leere Antwort");
            http.end();
            lv_obj_remove_state(load_btn, LV_STATE_DISABLED);
            return;
        }
        // Alten Buffer freigeben
        if (jpeg_buf) { free(jpeg_buf); jpeg_buf = NULL; }
        jpeg_buf = (uint8_t *)ps_malloc(content_len);
        if (!jpeg_buf) {
            lv_label_set_text(status_label, LV_SYMBOL_WARNING "  Kein Speicher!");
            http.end();
            lv_obj_remove_state(load_btn, LV_STATE_DISABLED);
            return;
        }
        memcpy(jpeg_buf, payload.c_str(), content_len);
        jpeg_len = content_len;
    } else {
        // Content-Length bekannt — direkt in PSRAM lesen
        if (jpeg_buf) { free(jpeg_buf); jpeg_buf = NULL; }
        jpeg_buf = (uint8_t *)ps_malloc(content_len);
        if (!jpeg_buf) {
            lv_label_set_text(status_label, LV_SYMBOL_WARNING "  Kein Speicher!");
            http.end();
            lv_obj_remove_state(load_btn, LV_STATE_DISABLED);
            return;
        }

        WiFiClient *stream = http.getStreamPtr();
        uint32_t read_total = 0;
        while (read_total < (uint32_t)content_len) {
            int avail = stream->available();
            if (avail > 0) {
                int to_read = min(avail, (int)(content_len - read_total));
                int rd = stream->readBytes(jpeg_buf + read_total, to_read);
                read_total += rd;
            } else {
                delay(10);
            }
        }
        jpeg_len = read_total;
    }
    http.end();

    Serial.printf("[IMG] Downloaded %u bytes\n", jpeg_len);

    // JPEG-Dimensionen aus SOF-Marker lesen
    uint16_t img_w = 0, img_h = 0;
    if (!jpeg_get_dimensions(jpeg_buf, jpeg_len, &img_w, &img_h)) {
        Serial.println("[IMG] JPEG header parse failed!");
        lv_label_set_text(status_label, LV_SYMBOL_WARNING "  JPEG-Header ungueltig");
        lv_obj_remove_state(load_btn, LV_STATE_DISABLED);
        return;
    }
    Serial.printf("[IMG] JPEG dimensions: %ux%u\n", img_w, img_h);

    // LVGL image descriptor aufbauen — w/h MUESSEN gesetzt sein!
    memset(&img_dsc, 0, sizeof(img_dsc));
    img_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    img_dsc.header.cf     = LV_COLOR_FORMAT_RAW;
    img_dsc.header.w      = img_w;
    img_dsc.header.h      = img_h;
    img_dsc.header.stride = img_w * 3;
    img_dsc.data_size     = jpeg_len;
    img_dsc.data          = jpeg_buf;

    lv_image_set_src(img_widget, &img_dsc);
    lv_obj_align(img_widget, LV_ALIGN_CENTER, 0, 15);

    lv_label_set_text_fmt(status_label, LV_SYMBOL_OK "  %u KB geladen", jpeg_len / 1024);
    lv_obj_remove_state(load_btn, LV_STATE_DISABLED);
}

void image_screen_from_inet_create(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_IMAGE "  Bild aus dem Internet");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 15, 12);

    load_btn = lv_button_create(parent);
    lv_obj_set_size(load_btn, 130, 36);
    lv_obj_align(load_btn, LV_ALIGN_TOP_RIGHT, -10, 8);
    lv_obj_add_event_cb(load_btn, do_download, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_lbl = lv_label_create(load_btn);
    lv_label_set_text(btn_lbl, "Bild laden");
    lv_obj_center(btn_lbl);

    status_label = lv_label_create(parent);
    lv_label_set_text(status_label, "Tippe 'Bild laden' (WLAN noetig)");
    lv_obj_set_width(status_label, 450);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 15, 50);

    img_widget = lv_image_create(parent);
    lv_obj_align(img_widget, LV_ALIGN_CENTER, 0, 15);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "< swipe to navigate >");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -15);
}

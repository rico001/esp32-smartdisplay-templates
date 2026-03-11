#include "wifi_screen.h"
#include <WiFi.h>
#include <Preferences.h>
#include <lvgl.h>

// ============================================================
// State Machine
// ============================================================
enum wifi_state_t {
    WS_IDLE,            // Nicht verbunden
    WS_AUTO_CONNECTING, // Versucht gespeicherte Credentials
    WS_SCANNING,        // Netzwerk-Scan laeuft
    WS_SCAN_RESULTS,    // Netzwerke gefunden, User waehlt
    WS_PASSWORD_INPUT,  // Netzwerk gewaehlt, Passwort-Eingabe
    WS_CONNECTING,      // Manueller Verbindungsversuch laeuft
    WS_CONNECTED,       // Verbunden
};

static wifi_state_t state = WS_IDLE;

// ============================================================
// UI Widgets
// ============================================================
static lv_obj_t *network_list;
static lv_obj_t *password_ta;
static lv_obj_t *keyboard;
static lv_obj_t *status_label;
static lv_obj_t *connect_btn;
static lv_obj_t *scan_btn;
static lv_obj_t *forget_btn;
static lv_obj_t *reconnect_btn;
static lv_obj_t *reconnect_lbl;
static lv_obj_t *password_cont;

// ============================================================
// Interner State
// ============================================================
static char selected_ssid[33];
static char pending_password[65];
static uint8_t connect_attempts;
static uint8_t reconnect_count = 0;
#define MAX_RECONNECTS 3
#define MAX_SCAN_RESULTS 20
static char scan_ssids[MAX_SCAN_RESULTS][33];

// Timer — nur diese beiden werden dynamisch erzeugt/geloescht
static lv_timer_t *connect_timer = NULL;
static lv_timer_t *scan_timer = NULL;
// Deferred-Timer fuer disconnect→begin Kette
static lv_timer_t *deferred_timer = NULL;
// Watchdog laeuft IMMER, wird nie geloescht
static lv_timer_t *watchdog_timer = NULL;

static Preferences prefs;

// ============================================================
// Forward Declarations
// ============================================================
static void enter_state(wifi_state_t new_state);
static void stop_connect_timers();
static void start_connect(bool is_auto);
static void start_scan();
static void deferred_disconnect_cb(lv_timer_t *timer);

// ============================================================
// NVS Credential Storage
// ============================================================

static void save_credentials(const char *ssid, const char *pw)
{
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pw", pw);
    prefs.end();
}

static bool load_credentials(char *ssid, size_t ssid_len, char *pw, size_t pw_len)
{
    prefs.begin("wifi", true);
    String s = prefs.getString("ssid", "");
    String p = prefs.getString("pw", "");
    prefs.end();

    if (s.length() == 0) return false;

    strncpy(ssid, s.c_str(), ssid_len - 1);
    ssid[ssid_len - 1] = '\0';
    strncpy(pw, p.c_str(), pw_len - 1);
    pw[pw_len - 1] = '\0';
    return true;
}

static void clear_credentials()
{
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();
    Serial.println("[WiFi] credentials cleared");
}

static bool has_saved_credentials()
{
    prefs.begin("wifi", true);
    String s = prefs.getString("ssid", "");
    prefs.end();
    return s.length() > 0;
}

// ============================================================
// Timer Management — Watchdog wird NIE getoetet
// ============================================================

static void stop_connect_timers()
{
    if (connect_timer)  { lv_timer_delete(connect_timer);  connect_timer  = NULL; }
    if (deferred_timer) { lv_timer_delete(deferred_timer); deferred_timer = NULL; }
}

static void stop_scan_timer()
{
    if (scan_timer) { lv_timer_delete(scan_timer); scan_timer = NULL; }
}

static void stop_all_action_timers()
{
    stop_connect_timers();
    stop_scan_timer();
    // watchdog_timer bleibt IMMER am leben!
}

// ============================================================
// UI Helpers
// ============================================================

static void show_password_input()
{
    lv_obj_remove_flag(password_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_state(connect_btn, LV_STATE_DISABLED);
}

static void hide_password_input()
{
    lv_obj_add_flag(password_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================
// State Transitions
// ============================================================

static void enter_state(wifi_state_t new_state)
{
    state = new_state;

    // Alles verstecken, dann nur das noetige einblenden
    hide_password_input();
    lv_obj_add_flag(network_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(forget_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(reconnect_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_state(scan_btn, LV_STATE_DISABLED);

    switch (state) {
    case WS_IDLE: {
        bool has_creds = has_saved_credentials();
        if (has_creds) {
            char ssid[33], pw_tmp[65];
            load_credentials(ssid, sizeof(ssid), pw_tmp, sizeof(pw_tmp));
            lv_label_set_text_fmt(reconnect_lbl, LV_SYMBOL_REFRESH "  Neuverbinden\n%s", ssid);
            lv_obj_remove_flag(reconnect_btn, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(status_label, "Nicht verbunden");
        } else {
            lv_label_set_text(status_label, "Tippe 'Scannen' um zu starten");
        }
        break;
    }

    case WS_AUTO_CONNECTING:
        lv_label_set_text_fmt(status_label, "Verbinde mit %s ...", selected_ssid);
        break;

    case WS_SCANNING:
        lv_obj_add_state(scan_btn, LV_STATE_DISABLED);
        lv_label_set_text(status_label, "Scanne Netzwerke...");
        break;

    case WS_SCAN_RESULTS:
        lv_obj_remove_flag(network_list, LV_OBJ_FLAG_HIDDEN);
        // Status-Text wird von scan_poll_cb gesetzt
        break;

    case WS_PASSWORD_INPUT:
        show_password_input();
        lv_textarea_set_text(password_ta, "");
        lv_label_set_text_fmt(status_label, "Netzwerk: %s", selected_ssid);
        break;

    case WS_CONNECTING:
        show_password_input();
        lv_obj_add_state(connect_btn, LV_STATE_DISABLED);
        lv_label_set_text_fmt(status_label, "Verbinde mit %s ...", selected_ssid);
        break;

    case WS_CONNECTED:
        lv_obj_remove_flag(forget_btn, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(status_label,
            LV_SYMBOL_WIFI "  Verbunden mit %s\nIP: %s",
            selected_ssid, WiFi.localIP().toString().c_str());
        break;
    }
}

// ============================================================
// Connection Logic
// ============================================================

static void connect_failed(bool is_auto, const char *reason)
{
    Serial.printf("[WiFi] connect failed: %s (attempt %d/%d)\n",
                  reason, reconnect_count, MAX_RECONNECTS);
    stop_connect_timers();
    WiFi.disconnect();

    if (is_auto && reconnect_count < MAX_RECONNECTS) {
        // Automatisch erneut versuchen nach kurzer Pause
        reconnect_count++;
        lv_label_set_text_fmt(status_label,
            LV_SYMBOL_WARNING "  %s\nReconnect %d/%d ...",
            reason, reconnect_count, MAX_RECONNECTS);
        // Retry via deferred chain (bleibt in WS_AUTO_CONNECTING)
        deferred_timer = lv_timer_create(deferred_disconnect_cb, 2000, NULL);
        lv_timer_set_repeat_count(deferred_timer, 1);
    } else if (is_auto) {
        // Alle Versuche aufgebraucht — Button zeigen
        Serial.println("[WiFi] reconnect gave up, showing button");
        reconnect_count = 0;
        enter_state(WS_IDLE);
    } else {
        enter_state(WS_PASSWORD_INPUT);
        lv_label_set_text_fmt(status_label, LV_SYMBOL_WARNING "  %s", reason);
    }
}

static void check_connection_cb(lv_timer_t *t)
{
    wl_status_t ws = WiFi.status();
    connect_attempts++;

    bool is_auto = (state == WS_AUTO_CONNECTING);
    uint8_t max_attempts = is_auto ? 20 : 30; // 10s auto, 15s manuell

    if (ws == WL_CONNECTED) {
        stop_connect_timers();
        reconnect_count = 0;
        save_credentials(selected_ssid, pending_password);
        enter_state(WS_CONNECTED);
        return;
    }

    // Erst nach ein paar Checks als echten Fehler werten
    // (direkt nach WiFi.begin() kann der Status noch vom vorherigen Versuch stammen)
    if (connect_attempts >= 5 && (ws == WL_CONNECT_FAILED || ws == WL_NO_SSID_AVAIL)) {
        connect_failed(is_auto,
            ws == WL_NO_SSID_AVAIL ? "Netzwerk nicht gefunden" : "Verbindung fehlgeschlagen");
        return;
    }

    // Timeout
    if (connect_attempts >= max_attempts) {
        connect_failed(is_auto, "Zeitueberschreitung");
    }
}

static void deferred_begin_cb(lv_timer_t *timer)
{
    deferred_timer = NULL;
    connect_attempts = 0;
    WiFi.begin(selected_ssid, pending_password);

    connect_timer = lv_timer_create(check_connection_cb, 500, NULL);
    lv_timer_set_repeat_count(connect_timer, -1);
}

static void deferred_disconnect_cb(lv_timer_t *timer)
{
    deferred_timer = NULL;
    WiFi.disconnect();
    deferred_timer = lv_timer_create(deferred_begin_cb, 500, NULL);
    lv_timer_set_repeat_count(deferred_timer, 1);
}

static void start_connect(bool is_auto)
{
    stop_all_action_timers();
    enter_state(is_auto ? WS_AUTO_CONNECTING : WS_CONNECTING);

    deferred_timer = lv_timer_create(deferred_disconnect_cb, 50, NULL);
    lv_timer_set_repeat_count(deferred_timer, 1);
}

// ============================================================
// Scan Logic
// ============================================================

static void network_btn_cb(lv_event_t *e)
{
    // current_target = der Button (auf dem der Callback registriert ist)
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
    int32_t idx = lv_obj_get_index(btn);

    if (idx < 0 || idx >= MAX_SCAN_RESULTS) return;

    char *ssid = scan_ssids[idx];

    if (strlen(ssid) == 0) return;
    strncpy(selected_ssid, ssid, sizeof(selected_ssid) - 1);
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';
    enter_state(WS_PASSWORD_INPUT);
}

static void scan_poll_cb(lv_timer_t *timer)
{
    int16_t n = WiFi.scanComplete();

    if (n == WIFI_SCAN_RUNNING) return;

    scan_timer = NULL; // Timer wird gleich geloescht oder laeuft ab
    lv_timer_delete(timer);

    if (n == WIFI_SCAN_FAILED || n == 0) {
        lv_label_set_text(status_label, "Keine Netzwerke gefunden");
        enter_state(WS_IDLE);
        return;
    }

    lv_obj_clean(network_list);
    int unique_count = 0;

    for (int i = 0; i < n && unique_count < MAX_SCAN_RESULTS; i++) {
        if (WiFi.SSID(i).length() == 0) continue;
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (WiFi.SSID(i) == WiFi.SSID(j)) { dup = true; break; }
        }
        if (dup) continue;

        String ssid_str = WiFi.SSID(i);
        strncpy(scan_ssids[unique_count], ssid_str.c_str(), 32);
        scan_ssids[unique_count][32] = '\0';

        const char *icon = LV_SYMBOL_WIFI;
        if (WiFi.RSSI(i) < -80) icon = LV_SYMBOL_WARNING;

        lv_obj_t *btn = lv_list_add_button(network_list, icon, scan_ssids[unique_count]);
        // Kein user_data mehr — Index-basiert in network_btn_cb
        lv_obj_add_event_cb(btn, network_btn_cb, LV_EVENT_CLICKED, NULL);
        unique_count++;
    }

    WiFi.scanDelete();
    lv_label_set_text_fmt(status_label, "%d Netzwerk(e) gefunden", unique_count);
    enter_state(WS_SCAN_RESULTS);
}

static void start_scan()
{
    stop_all_action_timers();
    WiFi.disconnect();
    lv_obj_clean(network_list);

    enter_state(WS_SCANNING);

    WiFi.scanNetworks(true);
    scan_timer = lv_timer_create(scan_poll_cb, 300, NULL);
    lv_timer_set_repeat_count(scan_timer, 50); // max ~15s
}

// ============================================================
// Watchdog: laeuft IMMER, prueft nur in WS_CONNECTED
// ============================================================

static void watchdog_cb(lv_timer_t *timer)
{
    (void)timer;
    if (state != WS_CONNECTED) return;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] connection lost! starting auto-reconnect...");
        reconnect_count = 0;
        start_connect(true);
    }
}

// ============================================================
// Button Callbacks
// ============================================================

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    start_scan();
}

static void connect_btn_cb(lv_event_t *e)
{
    (void)e;
    if (state != WS_PASSWORD_INPUT) return;

    const char *pw = lv_textarea_get_text(password_ta);
    strncpy(pending_password, pw, sizeof(pending_password) - 1);
    pending_password[sizeof(pending_password) - 1] = '\0';

    start_connect(false);
}

static void reconnect_btn_cb(lv_event_t *e)
{
    (void)e;
    char ssid[33], pw[65];
    if (load_credentials(ssid, sizeof(ssid), pw, sizeof(pw))) {
        strncpy(selected_ssid, ssid, sizeof(selected_ssid));
        strncpy(pending_password, pw, sizeof(pending_password));
        start_connect(true);
    }
}

static void forget_btn_cb(lv_event_t *e)
{
    (void)e;
    stop_all_action_timers();
    WiFi.disconnect();
    clear_credentials();
    enter_state(WS_IDLE);
}

static void ta_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        connect_btn_cb(e);
    }
}

// ============================================================
// Public API
// ============================================================

void wifi_screen_create(lv_obj_t *parent)
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(200); // WiFi-Hardware braucht Zeit nach mode(STA)

    // --- Titel ---
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  WLAN");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 15, 12);

    // --- Scan-Button (immer oben rechts) ---
    scan_btn = lv_button_create(parent);
    lv_obj_set_size(scan_btn, 120, 36);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -10, 8);
    lv_obj_add_event_cb(scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, "Scannen");
    lv_obj_center(scan_lbl);

    // --- Vergessen-Button (neben Scan, anfangs versteckt) ---
    forget_btn = lv_button_create(parent);
    lv_obj_set_size(forget_btn, 120, 36);
    lv_obj_align(forget_btn, LV_ALIGN_TOP_RIGHT, -140, 8);
    lv_obj_set_style_bg_color(forget_btn, lv_color_hex(0xCC3333), 0);
    lv_obj_add_event_cb(forget_btn, forget_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(forget_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *forget_lbl = lv_label_create(forget_btn);
    lv_label_set_text(forget_lbl, "Vergessen");
    lv_obj_center(forget_lbl);

    // --- Neuverbinden-Button (anfangs versteckt) ---
    reconnect_btn = lv_button_create(parent);
    lv_obj_set_size(reconnect_btn, 250, 50);
    lv_obj_align(reconnect_btn, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_bg_color(reconnect_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_add_event_cb(reconnect_btn, reconnect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(reconnect_btn, LV_OBJ_FLAG_HIDDEN);

    reconnect_lbl = lv_label_create(reconnect_btn);
    lv_label_set_text(reconnect_lbl, "Neuverbinden");
    lv_obj_set_style_text_align(reconnect_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(reconnect_lbl);

    // --- Status-Label ---
    status_label = lv_label_create(parent);
    lv_label_set_text(status_label, "");
    lv_obj_set_width(status_label, 450);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 15, 50);

    // --- Netzwerk-Liste (anfangs versteckt) ---
    network_list = lv_list_create(parent);
    lv_obj_set_size(network_list, 460, 150);
    lv_obj_align(network_list, LV_ALIGN_TOP_MID, 0, 115);
    lv_obj_add_flag(network_list, LV_OBJ_FLAG_HIDDEN);

    // --- Passwort-Bereich (anfangs versteckt, gleiche Position wie Liste) ---
    password_cont = lv_obj_create(parent);
    lv_obj_set_size(password_cont, 460, 50);
    lv_obj_align(password_cont, LV_ALIGN_TOP_MID, 0, 115);
    lv_obj_set_flex_flow(password_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(password_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(password_cont, 5, 0);
    lv_obj_add_flag(password_cont, LV_OBJ_FLAG_HIDDEN);

    password_ta = lv_textarea_create(password_cont);
    lv_textarea_set_placeholder_text(password_ta, "Passwort (leer = offen)");
    lv_textarea_set_password_mode(password_ta, true);
    lv_textarea_set_one_line(password_ta, true);
    lv_obj_set_flex_grow(password_ta, 1);
    lv_obj_set_height(password_ta, 40);
    lv_obj_add_event_cb(password_ta, ta_event_cb, LV_EVENT_READY, NULL);

    connect_btn = lv_button_create(password_cont);
    lv_obj_set_size(connect_btn, 120, 40);
    lv_obj_add_event_cb(connect_btn, connect_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *conn_lbl = lv_label_create(connect_btn);
    lv_label_set_text(conn_lbl, "Verbinden");
    lv_obj_center(conn_lbl);

    // --- Keyboard (anfangs versteckt) ---
    keyboard = lv_keyboard_create(parent);
    lv_obj_set_size(keyboard, 480, 190);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, password_ta);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    // --- Watchdog: laeuft IMMER im Hintergrund, wird NIE geloescht ---
    watchdog_timer = lv_timer_create(watchdog_cb, 5000, NULL);
    lv_timer_set_repeat_count(watchdog_timer, -1);

    // --- Startup ---
    char ssid[33], pw[65];
    if (load_credentials(ssid, sizeof(ssid), pw, sizeof(pw))) {
        Serial.printf("[WiFi] auto-connect: '%s'\n", ssid);
        strncpy(selected_ssid, ssid, sizeof(selected_ssid));
        strncpy(pending_password, pw, sizeof(pending_password));
        start_connect(true);
    } else {
        enter_state(WS_IDLE);
    }
}

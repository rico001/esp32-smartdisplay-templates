#ifndef WIFI_SCREEN_H
#define WIFI_SCREEN_H

#include <lvgl.h>

// Erstellt den WiFi-Screen auf dem gegebenen Parent-Objekt (z.B. ein Tileview-Tile).
// Zeigt verfuegbare Netzwerke, Passwort-Eingabe und Verbindungsstatus.
void wifi_screen_create(lv_obj_t *parent);

#endif

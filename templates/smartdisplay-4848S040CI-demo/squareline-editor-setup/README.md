# Nutzung mit SquareLine Studio Editor

## Projekteinstellungen in SquareLine Studio

- **LVGL Version**: 9.2.2
- **Display Resolution**: 480 x 480
- **Color Depth**: 16-bit (RGB565)

## Exportierte Dateien

Nach dem Export aus SquareLine Studio liegen die generierten Dateien in `SquareLine_Project/`:

| Datei | Beschreibung |
|---|---|
| `ui.h` / `ui.c` | Haupt-UI-Initialisierung (`ui_init()`, `ui_destroy()`) |
| `ui_Screen*.h/.c` | Einzelne Screen-Definitionen |
| `ui_helpers.h/.c` | Hilfsfunktionen fuer Animationen, Properties etc. |
| `ui_events.h` | Event-Callback-Deklarationen |
| `ui_comp_hook.c` | Component-Hook |

## Integration ins PlatformIO-Projekt

1. **Dateien kopieren** — Alle `.c`- und `.h`-Dateien aus `SquareLine_Project/` nach `src/ui/`:

   ```bash
   cp squareline-editor-setup/SquareLine_Project/*.c squareline-editor-setup/SquareLine_Project/*.h src/ui/
   ```

2. **main.cpp** — Muss nur `ui_init()` aufrufen:

   ```cpp
   #include <Arduino.h>
   #include <esp32_smartdisplay.h>
   #include "ui/ui.h"

   auto lv_last_tick = millis();

   void setup() {
       Serial.begin(115200);
       smartdisplay_init();
       ui_init();
   }

   void loop() {
       auto const now = millis();
       lv_tick_inc(now - lv_last_tick);
       lv_last_tick = now;
       lv_timer_handler();
   }
   ```

3. **Build** — Ganz normal mit `pio run` kompilieren.

## Workflow bei UI-Aenderungen

1. UI in SquareLine Studio bearbeiten
2. Exportieren (die Dateien landen in `SquareLine_Project/`)
3. Dateien nach `src/ui/` kopieren (ueberschreiben)
4. `pio run` zum Kompilieren, `pio run --target upload` zum Flashen

**Wichtig**: Die Dateien in `src/ui/` nicht manuell editieren — sie werden beim naechsten Export ueberschrieben. Eigene Logik (Event-Handler etc.) in separate Dateien unter `src/` schreiben.

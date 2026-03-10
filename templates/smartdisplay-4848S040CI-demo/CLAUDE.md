# CLAUDE.md — Projektnotizen für Claude

## Projektüberblick

PlatformIO-Projekt (Arduino-Framework) für ein Sunton ESP32-S3 Display-Board. Nutzt die [esp32-smartdisplay](https://github.com/rzeldent/esp32-smartdisplay) Library als Display/Touch-Treiber und LVGL 9.2.2 als GUI-Framework.

### Projektstruktur
```
smartdisplay-4848S040CI-demo/
├── boards/              # Git-Submodul: Board-Definitionen (platformio-espressif32-sunton)
├── include/
│   └── lv_conf.h        # LVGL-Konfiguration (Fonts, Widgets, Farbtiefe etc.)
├── src/
│   └── main.cpp         # Hauptprogramm — Screens werden hier definiert
├── backup-initial-demo/ # Firmware-Backup vom Werkszustand
├── pre_build.py         # Workaround: entfernt ARM-Assembly aus LVGL
├── platformio.ini       # Build-Konfiguration
└── CLAUDE.md
```

### Workflow
- `pio run` — kompilieren
- `pio run --target upload` — auf Board flashen (dauert ~30s)
- `pio device monitor` — Serial-Output anschauen
- Nach Änderungen an `lv_conf.h`: `.pio/`-Ordner löschen vor dem Build

### Code-Architektur
- `main.cpp` enthält `setup()` und `loop()` plus Screen-Funktionen
- Screens werden als eigene Funktionen geschrieben: `create_screen_xyz(lv_obj_t *parent)`
- Navigation zwischen Screens läuft über einen LVGL Tileview (horizontal swipen)
- Neue Screens: Tile in `setup()` hinzufügen + Funktion erstellen + aufrufen
- Wenn der Code wächst, Screens in eigene `.cpp`/`.h`-Dateien unter `src/` auslagern

### Gepinnte Versionen (nicht ändern ohne zu testen)
- `espressif32 @ 6.9.0`
- `lvgl/lvgl @ 9.2.2`
- `esp32_smartdisplay` via Git (aktuell v2.1.1)

## Board

- **Sunton ESP32-4848S040CI** (ESP32-S3, 480x480 Display, Capacitive Touch, 16MB Flash)
- PlatformIO Board-Name: `esp32-4848S040CIY1`
- USB-Port (macOS): `/dev/cu.usbserial-10`

## Kritische Fallstricke

### Ordnername darf nicht `ESP32` enthalten
`ESP32` ist ein Preprocessor-Macro (`#define ESP32 1`). Wenn der Projektordner `ESP32` im Namen hat, wird der Pfad zur `lv_conf.h` vom Compiler verstümmelt (z.B. `ESP32-foo` → `1-foo`). Deshalb heißt der Ordner `smartdisplay-...` statt `ESP32-...`.

### ARM-Assembly-Dateien in LVGL manuell entfernen
LVGL liefert ARM-spezifische `.S`-Dateien (Helium, NEON) mit, die PlatformIO fälschlicherweise mit dem Xtensa-Assembler kompiliert. Die `#if`-Guards im Code helfen nicht, weil der Assembler schon bei den `#include`-Direktiven scheitert. Das `pre_build.py`-Script entfernt diese Dateien automatisch.

### Platform-Version muss gepinnt werden
`espressif32 @ 6.9.0` verwenden. Neuere Versionen (55.x pioarduino, ESP-IDF 5.5+) haben Breaking API Changes (`disp_off` → `disp_on_off`), die die esp32-smartdisplay Library noch nicht unterstützt.

### LVGL-Version muss explizit gepinnt werden
Die Library gibt `^9.2.2` an, PlatformIO zieht dann aber 9.5.0 — was nicht kompatibel ist. In `lib_deps` explizit `lvgl/lvgl @ 9.2.2` angeben.

### LV_CONF_PATH ohne Quotes
Bei LVGL 9.2.x das `-D LV_CONF_PATH=...` Flag OHNE Anführungszeichen um den Pfad setzen. Das `__LV_TO_STR`-Macro stringifiziert selbst. Mit Quotes gibt es `#include expects "FILENAME"` Fehler.

### lv_conf.h Änderungen erfordern Clean Build
Nach Änderungen an `lv_conf.h` den `.pio/`-Ordner löschen — Libraries werden gecacht und übernehmen Änderungen sonst nicht.

## USB / Flashen

- Flash-Download (read_flash): Nur 115200 Baud stabil. 460800 und 921600 führen zu Corrupt-Data-Fehlern bei diesem Board.
- Flash-Upload (write_flash / pio upload): Funktioniert mit Standard-Baudrate problemlos.
- Firmware-Backup der Original-Demo liegt in `backup-initial-demo/`.
- esptool.py muss über PlatformIO-Python aufgerufen werden: `~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py`

## LVGL-Hinweise

- `LV_COLOR_DEPTH 16` (RGB565) — einzig unterstütztes Format auf diesen Panels
- `lv_dir_t` Enums müssen bei Kombination gecastet werden: `(lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT)`
- Display ist 480x480 Pixel (quadratisch)

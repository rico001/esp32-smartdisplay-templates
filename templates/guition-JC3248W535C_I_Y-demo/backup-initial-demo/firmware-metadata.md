# Demo Firmware Metadata — Guition JC3248W535C_I_Y Werksfirmware

Analyse der `firmware_full_16MB.bin` basierend auf String-Extraktion aus der Binary.

## Checksums

| Algorithmus | Hash |
|-------------|------|
| MD5         | `79d089f7c5b682ef2080c4a3aac48c4a` |
| SHA-256     | `fed679e8d5177a096d4978695cfb3842f338bafd71f0878309edef1adcd46018` |
| Groesse     | 16.777.216 Bytes (16 MB, vollstaendiger Flash-Dump) |

## Build-Umgebung

| Eigenschaft | Wert |
|-------------|------|
| Framework   | Arduino (`framework-arduinoespressif32`, via `arduino-lib-builder`) |
| Platform    | PlatformIO auf Windows (`C:/Users/Fei/.platformio/`) |
| ESP-IDF     | v5.1.4-497-g`dc859c1e67`-dirty (eingebettet im Arduino-Framework) |
| Chip        | ESP32-S3 |

## GUI / Display

| Eigenschaft | Wert |
|-------------|------|
| GUI-Framework | **LVGL** (Version nicht explizit in Binary, vermutlich 8.x basierend auf API-Mustern wie `lvgl_port_*`) |
| Display-Library | **ESP-LCD** (`esp_lcd_new_panel_axs15231b`, eigener Treiber unter `/src/driver/screen_init/axs15231b/`) |
| Touch-Treiber | AXS15231B integriert (`esp_lcd_touch.c`, I2C) |
| Display-Interface | QSPI (`esp_lcd_new_panel_io_spi`) |
| UI-System | Eigene App-Architektur mit mehreren Screens (Wetter, AIDA64, Einstellungen etc.) |

**Hinweis zum Framework:** Die Werksfirmware nutzt **nicht** die Arduino_GFX Library, sondern den nativen **ESP-LCD Treiber** (`esp_lcd_panel_*` API) mit einem eigenen AXS15231B-Panel-Treiber. Das ist ein voellig anderer Ansatz als unser Projekt (Arduino_GFX + Canvas).

## Funktionen der Demo-Firmware

### Wetter-App (Hauptfunktion)

- Wetteranzeige ueber **OpenWeatherMap API** (`api.openweathermap.org`)
- Konfigurierbar per City Code und API Key
- Automatisches Retry bei Fehler mit konfigurierbarem Intervall
- Unterstuetzt Stadtnamen-Anzeige (`CityName`)

### AIDA64 PC-Monitoring

- Empfaengt PC-Hardware-Daten von **AIDA64** ueber Netzwerk (UDP)
- Konfigurierbar per Host-IP (`aidaip`)
- Zeigt CPU/GPU/RAM-Infos auf dem Display an
- Retry-Logik bei Verbindungsfehler

### Konnektivitaet

- **WiFi Station Mode**: Verbindet sich mit gespeichertem WLAN (NVS-Persistenz)
- **WiFi AP Mode**: Startet eigenen Access Point zur Erstkonfiguration
- **WiFi-Konfigurationsportal**: Eingebettete Webseite zum Scannen und Verbinden mit WLANs
  - HTML/CSS/JS direkt in der Firmware eingebettet
  - SSID-Scan mit Listendarstellung
  - Passwort-Eingabe
- **Control Panel**: Zweite eingebettete Webseite fuer Einstellungen
  - Helligkeit (`/save?brightness=`)
  - Lautstaerke (`/save?volume=`)
  - Wetter City Code + API Key (`/saveweather?citycode=&apikey=`)
  - AIDA64 Host-IP (`/save?hostip=`)
  - Datum/Uhrzeit-Sync vom Browser (`/save?timestamp=`)
  - Kommando-Ausfuehrung (`/docmd?cmd=`)
  - Status-Abfrage (`/getstatus`)
  - Zweisprachig: Englisch / Chinesisch

### Weitere Features

- **MP3-Wiedergabe**: Audio-Decoder `arduino-libhelix` v0.8.3, Repeat-Funktion, Play-Modes (vermutlich Alarmtoene/Benachrichtigungen)
- **OTA-Updates**: Over-the-Air Firmware-Update Unterstuetzung (`esp_ota_ops`)
- **NVS-Speicher**: WiFi-Credentials und Einstellungen persistent gespeichert
- **Helligkeitsregelung**: Per LEDC PWM, auch per UDP steuerbar
- **Lautstaerkeregelung**: Per DAC, Gain/Offset konfigurierbar
- **JPEG-Dekodierung**: Bilddekodierung mit Rotationsunterstuetzung (0/90/180/270 Grad)
- **Zweisprachig**: Englisch und Chinesisch (umschaltbar per Control Panel, `langid`)
- **SD-Karte**: MMC/SPI Flash Unterstuetzung (fuer MP3-Dateien)

## Hardware-Konfiguration

### Display

| Eigenschaft | Wert |
|-------------|------|
| Display-Controller | AXS15231B (eigener ESP-LCD Treiber) |
| Display-Interface | QSPI (SPI Host) |
| Aufloesung | 320x480 Pixel (nativ Portrait) |
| Farbtiefe | RGB565 (16 Bit) |

### Touch

| Eigenschaft | Wert |
|-------------|------|
| Touch-Controller | **AXS15231B** (integriert, Single-Chip fuer Display + Touch) |
| Interface | I2C (`esp_lcd_new_panel_io_i2c`) |

### Backlight

| Eigenschaft | Wert |
|-------------|------|
| Steuerung | LEDC PWM (softwareseitig regelbar, per Web-Panel und UDP) |

### Speicher

| Eigenschaft | Wert |
|-------------|------|
| Flash | 16 MB |
| PSRAM | Vorhanden (OPI, wird aktiv genutzt — Framebuffer, LVGL-Allokationen) |
| NVS | Ja (WiFi-Daten, Einstellungen) |
| Partitionen | Standard ESP-IDF Partitionstabelle mit OTA-Partitionen |

### Hinweis zu Pin-Belegungen

Die exakten GPIO-Zuweisungen konnten nicht aus der Binary extrahiert werden, da diese als Compile-Time-Konstanten direkt im Maschinencode landen. Die Pin-Belegung in unserer [README.md](../README.md) wurde unabhaengig durch Datenblaetter und Tests ermittelt.

## Unterschiede zu unserem Projekt

| Aspekt | Werksfirmware | Unser Projekt |
|--------|--------------|---------------|
| LVGL-Version | Vermutlich 8.x (basierend auf `lvgl_port_*` API) | 9.2.2 |
| Display-Library | ESP-LCD (`esp_lcd_panel_axs15231b`, eigener Treiber) | Arduino_GFX v1.6.0 (`Arduino_AXS15231B` + `Arduino_Canvas`) |
| Display-Ansteuerung | Nativer ESP-LCD QSPI-Treiber | Arduino_GFX QSPI-Bus + Canvas-Framebuffer |
| UI-Aufbau | Vollstaendige Produktions-App (Wetter, AIDA64, MP3, Web-Panel) | LVGL Showcase mit 6 Demo-Screens |
| WiFi | Station + AP Mode mit Web-Konfigurationsportal | Nicht implementiert |
| Audio | MP3-Wiedergabe mit libhelix | Nicht implementiert |
| OTA | Unterstuetzt | Nicht implementiert |
| Build-System | PlatformIO (Windows) | PlatformIO (macOS) |
| Sprachen | Englisch + Chinesisch | Deutsch (Code-Kommentare) |

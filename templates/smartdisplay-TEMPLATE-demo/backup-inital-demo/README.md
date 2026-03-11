# Firmware Backup

Backup der Original-Firmware eines ESP-Boards (z. B. Werkszustand / Demo-Firmware).

## Voraussetzungen

`esptool` muss installiert sein:

```bash
pip install esptool
```

## Wie wurde das Backup erstellt?

Vor dem Auslesen wurde überprüft, welcher Port und Chip erkannt wird.

### Seriellen Port prüfen

```bash
ls /dev/cu.usb*
```

Beispiel:

```
/dev/cu.usbserial-10
```

### Chip erkennen

```bash
esptool.py --port /dev/cu.usbserial-10 chip_id
```

Beispielausgabe:

```
Chip is ESP32-S3
```

**Hinweis:**  
Der erkannte Chip bestimmt den Parameter `--chip`.  
Beispiele:

```
ESP32     → --chip esp32
ESP32-S2  → --chip esp32s2
ESP32-S3  → --chip esp32s3
ESP32-C3  → --chip esp32c3
ESP8266   → --chip esp8266
```

### Flashgröße und Flash-Chip prüfen

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbserial-10 flash_id
```

Beispielausgabe:

```
Manufacturer: ef
Device: 4018
Detected flash size: 16MB
```

**Hinweis:**  
Die Flashgröße bestimmt die Länge des Dumps:

```
4MB  → 0x400000
8MB  → 0x800000
16MB → 0x1000000
```

Der Parameter `--chip` muss dem zuvor erkannten Chip entsprechen.  
Im Beispiel wird `esp32s3` verwendet.

### Firmware auslesen

Mit `esptool.py` wurde anschließend der gesamte Flash-Speicher ausgelesen.

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbserial-10 --baud 115200 read_flash 0x0 0x1000000 firmware_full_16MB.bin
```

- **Port**: `/dev/cu.usbserial-10` (kann bei dir anders sein, prüfe mit `ls /dev/cu.usb*`)
- **Baud**: 115200 (höhere Raten können je nach Board instabil sein)
- **Adresse**: `0x0` — Start des Flash
- **Größe**: `0x1000000` = 16MB (gesamter Flash-Speicher)
- **Dauer**: ca. 20 Minuten bei 115200 Baud

Optional kann man den Dump anschließend prüfen:

```bash
ls -lh firmware_full_16MB.bin
```

Die Datei sollte ungefähr der Größe des Flash-Speichers entsprechen (z. B. ~16MB).

---

## Wie kann man das Backup wiederherstellen?

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbserial-10 --baud 115200 write_flash 0x0 firmware_full_16MB.bin
```

Falls der Chip vorher gelöscht werden soll (empfohlen bei Problemen):

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbserial-10 erase_flash
```

Danach erneut flashen:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbserial-10 --baud 115200 write_flash 0x0 firmware_full_16MB.bin
```

---

## Hinweise

- Das Board muss per USB verbunden sein und ggf. in den Download-Modus versetzt werden (BOOT-Taste gedrückt halten beim Einschalten)
- Die Datei `firmware_full_16MB.bin` enthält den kompletten Flash-Dump.
#!/bin/sh
# setup.sh — Kopiert SquareLine-Export ins PlatformIO-Projekt und setzt main.cpp
#
# Ausfuehren aus dem Verzeichnis smartdisplay-4848S040CI-demo/:
#   sh squareline-editor-setup/setup.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SL_DIR="$SCRIPT_DIR/SquareLine_Project"
UI_DIR="$PROJECT_DIR/src/ui"
MAIN_SRC="$SCRIPT_DIR/___main.cpp"
MAIN_DST="$PROJECT_DIR/src/main.cpp"

# Pruefen ob SquareLine-Export vorhanden ist
if [ ! -d "$SL_DIR" ]; then
    echo "Fehler: $SL_DIR nicht gefunden."
    exit 1
fi

# 1) src/ui/ erstellen (falls noetig) und UI-Dateien kopieren
echo "Erstelle $UI_DIR ..."
mkdir -p "$UI_DIR"

echo "Kopiere .c/.h Dateien aus SquareLine_Project nach src/ui/ ..."
cp "$SL_DIR"/*.c "$SL_DIR"/*.h "$UI_DIR/"

# 2) main.cpp ersetzen
echo "Kopiere main.cpp ..."
cp "$MAIN_SRC" "$MAIN_DST"

echo ""
echo "Fertig! Dateien wurden kopiert:"
echo "  UI-Dateien -> $UI_DIR/"
echo "  main.cpp   -> $MAIN_DST"
echo ""
echo "Naechster Schritt: pio run --target upload -d $PROJECT_DIR"

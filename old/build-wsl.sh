#!/bin/bash
# Script pour compiler le projet Thermostat avec ESP-IDF sous WSL

# Sourcer l'environnement ESP-IDF
source ~/esp/esp-idf/export.sh

# Aller dans le répertoire du projet
cd /mnt/c/Users/NNJL0657/Projects/Stage3/Thermostat/Thermostat

# Compiler le projet
idf.py build

echo ""
echo "Pour flasher sur l'ESP32-C6, utilisez :"
echo "  idf.py -p /dev/ttyUSB0 flash monitor"
echo "  (remplacez /dev/ttyUSB0 par le bon port)"

Le projet consiste à réaliser un thermostat connecté permettant de piloter une chaudière et des têtes thermostatiques

# Description du materiel

 * Chaudiere piloter par fil pilote (un relais permet d'envoyer le signal de chauffe)
 * Rasberry Pi Zero : pour l'intelligence du thermostat
 * ESP32-C6-ZERO : pour gérer la communication Zigbee avec les têtes
 * Ecran tactile Waveshare 3,5"
 * Tête thermostatique : KETOTEK KTF0177
 * Communication RPI <-> ESP32 : UART
 * Capteur de temperature GY-BME280-3.3
 * Relais de pilotage de la chaudière : Panasonic AQY212GH
 * Chaudière Gaz ELM Leblanc — Chaudière gaz bas NOx murale ACLEIS BAS NOX NGLM 24-7XN, mixte 24kW, 11L/min


# Spécifications logiciel

* [Specifications protocol UART](SPECIFICATIONS-UART.md)
* [Specification ESP32-C6-UART](SPECIFICATIONS-ESP32-C6-ZERO.md)
* [Specification Raspberry PI Zero](SPECIFICATIONS-PIZERO.md)
* [Specification Programmation du chauffage](SPECIFICATIONS-PROGRAM.md)
* [Specification des écrans et de la navigation](SPECIFICATION-PIZERO-SCREEN.md)
* [Specification de l'API REST](SPECIFICATION-API.md)
* [Plan de câblage](SPECIFICATIONS-CABLAGE.md)
* [Installation et déploiement sur la Raspberry Pi Zero](INSTALLATION-PIZERO.md)

# Arborescence du code

/controller-zigbee : le code de L'ESP32-C6-ZERO
/thermostat : le code de la raspberry pi

Le répertoire `/prototype` contient l'ancien code de validation matérielle (gateway ESP32 + simulateur de tête, voir [prototype/SPECIFICATIONS.md](prototype/SPECIFICATIONS.md)) — gardé comme référence/exemple, ce n'est plus le code actif du projet.
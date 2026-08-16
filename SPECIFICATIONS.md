Le projet consiste à réaliser un thermostat connecté permettant de piloter une chaudière et des têtes thermostatiques

# Description du materiel

 * Chaudiere piloter par fil pilote (un relais permet d'envoyer le signal de chauffe)
 * Rasberry Pi Zero : pour l'intelligence du thermostat
 * ESP32-C6-ZERO : pour gérer la communication Zigbee avec les têtes
 * Ecran tactile Waveshare 3,5"
 * Tête thermostatique : KETOTEK KTF0177
 * Communication RPI <-> ESP32 : UART


# Spécifications logiciel

## Protocol UART
Le protocol UART va permettre une communication bidirectionnel entre la RPI et l'ESP32 pour piloter et remonter l'information des têtes thermostatiques. Les messages seront structurés en json avec un format : 
``` json
{
    "data": {},
    "sign" : "xxxx"
}
```
```sign``` contenant le hash du message ```data``` pour pouvoir s'assurer qu'il n'y a pas eut d'erreur de transmission

### Sens ESP32 vers RPI

#### Demande d'appairage
lorsque un device demande à s'appairer, un message est envoyé à la RPI avec les données d'identification du device (MAC, type, modèle ... )



## ESP32-C6-ZERO
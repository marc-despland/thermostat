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
Le protocol UART va permettre une communication bidirectionnelle entre la RPI et l'ESP32 pour piloter et remonter l'information des têtes thermostatiques. Le protocole applicatif (JSON + signature) et les types de messages ci-dessous s'appuient sur ce que le prototype (`/prototype`, voir `prototype/SPECIFICATIONS.md`) a validé côté radio Zigbee/Tuya avec une vraie tête KETOTEK KTF0177 : identification du device, DataPoints confirmés (mode, consigne, température mesurée, verrouillage enfant) et commandes gateway existantes ou restant à implémenter.

### Transport
* Liaison série 115200 bauds, 8N1, sans contrôle de flux matériel (à revoir si la distance/le câblage l'imposent)
* Un message = un objet JSON sérialisé sur une seule ligne, terminée par `\n` (NDJSON) : pas de préfixe de taille binaire, format simple à logguer/débugger tel quel
* Encodage UTF-8

### Enveloppe des messages
Les messages seront structurés en json avec un format : 
``` json
{
    "data": {
        "type": "...",
        "...": "..."
    },
    "sign" : "xxxx"
}
```
* ```sign``` contient le hash du message ```data``` (sérialisation JSON compacte, clés triées) pour pouvoir s'assurer qu'il n'y a pas eu d'erreur de transmission. Proposition : CRC16-CCITT en hexadécimal — assez robuste pour détecter une erreur de transmission sur une liaison UART point-à-point courte distance, et beaucoup plus léger à calculer sur l'ESP32-C6 qu'un hash cryptographique (disproportionné pour un lien local non exposé).
* ```data.type``` identifie le type de message (voir tables ci-dessous).
* Chaque tête thermostatique est référencée par son `short_addr` Zigbee (adresse réseau 16 bits, ex. `"0x3d6e"`) — c'est déjà l'identifiant utilisé côté gateway dans le prototype (`g_paired_devices[]`).

### Sens ESP32 vers RPI

| `type` | Déclenchement | Contenu de `data` |
|---|---|---|
| `pairing_request` | Signal Zigbee `ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE` (un device rejoint le réseau) | `short_addr`, `ieee_addr` (MAC), `manufacturer` (ex. `_TZE200_p3dbf6qs`), `model` (ex. `TS0601`) — lus via la lecture ZCL du cluster Basic (« magic packet », validée sur le KTF0177 réel) |
| `status_report` | Réception d'un rapport Tuya décodé (DataPoints) | `short_addr` et, selon les DP effectivement reçus dans la trame : `system_mode` (`auto`/`heat`/`off`, DP2), `running_state` (`heat`/`idle`, DP3), `heating_setpoint` (°C, DP4), `local_temp` (°C, DP5), `child_lock` (bool, DP7). Un rapport peut ne contenir qu'un sous-ensemble de ces champs. |
| `command_ack` | Accusé de réception d'une commande envoyée par la RPI | `command_id` (repris de la commande d'origine), `status` (`ok`/`error`), `error` (message, si `status = error`) |
| `device_left` | Départ/suppression d'un device du réseau Zigbee | `short_addr` |
| `devices_list` | Réponse à la commande `list_devices` | `devices` : tableau de `{ short_addr, ieee_addr, manufacturer, model }` |

### Sens RPI vers ESP32

| `type` | Effet côté gateway | Contenu de `data` |
|---|---|---|
| `set_heating_setpoint` | Écrit la consigne de chauffe (DP4) sur la tête | `short_addr`, `value` (°C, ex. `21.0`) |
| `set_system_mode` | Écrit le mode (DP2) sur la tête | `short_addr`, `value` (`auto`/`heat`/`off`) |
| `set_child_lock` | Écrit le verrouillage enfant (DP7) sur la tête | `short_addr`, `value` (bool) |
| `permit_join` | Ouvre la fenêtre d'appairage Zigbee | `duration_sec` (ex. `180`) |
| `list_devices` | Retourne la liste des devices appairés | *(vide)* |
| `remove_device` | Retire un device du réseau Zigbee | `short_addr` |

Chaque commande envoyée par la RPI porte un `command_id` (identifiant unique généré côté RPI, ex. compteur ou UUID court) afin de pouvoir corréler la `command_ack` correspondante.

### Notes d'implémentation
* Les champs de `status_report`/`set_*` reprennent uniquement les DataPoints **confirmés** sur le vrai KETOTEK KTF0177 dans le prototype (DP2/DP4/DP5/DP7 — voir `prototype/SPECIFICATIONS.md` § « DataPoints confirmés »). Les autres DP déjà décodés en lecture seule côté gateway (planning hebdomadaire DP28-34, protection hors-gel DP36, anti-entartrage DP39, calibration DP47, etc.) ne sont pas encore exposés dans ce protocole UART ; extension possible une fois un besoin identifié côté RPI/UI.
* `list_devices`, `remove_device` et `permit_join` correspondent aux commandes CLI encore non implémentées côté gateway dans le prototype (`TODO(THERMOSTAT_ENABLE_CLI)`) — il faudra les développer sur l'ESP32 avant de pouvoir les piloter depuis la RPI. `permit_join` peut s'appuyer directement sur `esp_zb_bdb_open_network()`, déjà utilisé au démarrage de la gateway.
* Pas de polling périodique prévu depuis la RPI pour lire l'état des têtes : comme observé sur le vrai KTF0177, les rapports Tuya sont spontanés, à la discrétion du firmware de la tête (pas de cadence fixe garantie, contrairement au simulateur du prototype qui rapporte toutes les 30s). La RPI doit donc consommer les `status_report` au fil de l'eau plutôt qu'attendre une réponse synchrone à une requête de lecture.
* Pas de commande Tuya « data query » fiable côté device réel (voir `prototype/SPECIFICATIONS.md` § « Erreur historique sur `0x11` ») : impossible de forcer une tête à renvoyer son état à la demande, y compris via ce protocole UART — seuls les rapports spontanés font foi.



## ESP32-C6-ZERO

L'ESP32 s'occupe de gérer la communication avec les têtes et la RPI. Elle implemente l'appairage d'un device et la gestion des différents DP en lecture et écriture via l'exposition du protocole UART.

Le rôle Zigbee et le décodage Tuya décrits ci-dessous reprennent tels quels ce qui a été validé sur matériel réel dans le prototype (`/prototype/gateway`, voir `prototype/SPECIFICATIONS.md`) ; seules l'exposition UART et quelques fonctions de gestion restent à écrire.

### Rôle Zigbee
* Coordinateur (`ESP_ZB_DEVICE_TYPE_COORDINATOR`, macro `ESP_ZB_ZC_CONFIG()`) — c'est l'ESP32 qui forme le réseau, les têtes s'y joignent en tant qu'End Device
* Radio native 802.15.4 de l'ESP32-C6 (`ZB_RADIO_MODE_NATIVE`), pas de co-processeur radio externe
* Canal fixé par compilation (25 par défaut) — pas de changement de canal à chaud sans reflash pour l'instant
* Endpoint 1, profil HA (`ESP_ZB_AF_HA_PROFILE_ID`), device ID thermostat (`ESP_ZB_HA_THERMOSTAT_DEVICE_ID`)
* Clusters exposés : Basic (`0x0000`), Identify, Power Config, Thermostat (`0x0201` — présent pour la conformité au profil HA mais non réellement câblé côté firmware Tuya des têtes, aucune donnée ne transite dessus), et le cluster propriétaire Tuya `0xEF00` en double rôle CLIENT+SERVER

### Appairage
* Au démarrage (factory-new ou simple reboot), le réseau est réouvert automatiquement pendant 180s (`esp_zb_bdb_open_network(180)`) — pas besoin de commande manuelle pour qu'une tête déjà en mode pairing puisse rejoindre
* Un nouveau device est détecté via le signal `ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE`, puis identifié par lecture ZCL standard du cluster Basic (« magic packet » : `manufacturerName`, `modelIdentifier`, `zclVersion`, `powerSource`...) — seule méthode d'identification fiable observée, le cluster Thermostat ne répondant pas
* Le device est ajouté à la table des devices appairés, et un message UART `pairing_request` est émis vers la RPI (adresse courte, adresse IEEE, fabricant, modèle)

### Lecture des données (DP)
* Réception des rapports Tuya spontanés du cluster `0xEF00` (commandes `TY_DATA_RESPONE` `0x01` et `TY_DATA_REPORT` `0x02`), décodage des DataPoints confirmés (DP2 mode, DP3 état de chauffe, DP4 consigne, DP5 température mesurée, DP7 verrouillage enfant) et traduction en message UART `status_report` vers la RPI
* Pas de mécanisme de lecture « à la demande » fiable : la commande Tuya `TY_DATA_QUERY` (`0x03`) reste sans réponse sur le matériel réel testé, et le cluster ZCL standard `0x0201` ne répond pas non plus — l'ESP32 se contente donc de relayer les rapports spontanés des têtes vers la RPI, il ne fait pas de polling actif vers elles

### Écriture des données (DP)
* Écriture confirmée fonctionnelle sur le matériel réel pour DP4 (consigne), via commande Tuya `TY_DATA_REQUEST` (`0x00`), type `VALUE`, valeur encodée ×10 en big-endian ; le même mécanisme est réutilisable pour DP2 (mode) et DP7 (verrouillage enfant)
* À la réception d'un message UART `set_heating_setpoint`/`set_system_mode`/`set_child_lock`, l'ESP32 construit la trame Tuya correspondante et l'envoie à la tête identifiée par son `short_addr`, puis renvoie un `command_ack` (succès, ou échec — ex. device inconnu ou injoignable)
* Type ZCL impérativement `ESP_ZB_ZCL_ATTR_TYPE_SET` (jamais `ARRAY`, qui réinterprète les 2 premiers octets comme un préfixe de taille et fait rejeter silencieusement la trame par la couche réseau — bug rencontré et corrigé dans le prototype)

### Fonctions restant à développer (au-delà de ce que valide le prototype)
* Exposition effective du protocole UART décrit ci-dessus (JSON + `sign`) : le prototype se contente de logguer les échanges Zigbee, il n'y a pas encore de liaison série vers une RPI
* Commandes `list_devices` / `remove_device` / `permit_join` pilotables depuis la RPI (dans le prototype, seule l'ouverture réseau au boot existe, pas de CLI)
* Persistance NVS de la table des devices appairés (en RAM dans le prototype, perdue à chaque reboot de l'ESP32)
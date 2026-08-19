## ESP32-C6-ZERO

L'ESP32 s'occupe de gérer la communication avec les têtes et la RPI. Elle implemente l'appairage d'un device et la gestion des différents DP en lecture et écriture via l'exposition du protocole UART.

Le rôle Zigbee et le décodage Tuya décrits ci-dessous reprennent tels quels ce qui a été validé sur matériel réel dans l'ancien prototype (`/prototype/gateway`, voir `prototype/SPECIFICATIONS.md` — gardé comme référence, code non actif) ; l'implémentation cible vit dans `/controller-zigbee` (voir l'arborescence du code dans [SPECIFICATIONS.md](SPECIFICATIONS.md)), où seules l'exposition UART et quelques fonctions de gestion restent à écrire.

### Rôle Zigbee
* Coordinateur (`ESP_ZB_DEVICE_TYPE_COORDINATOR`, macro `ESP_ZB_ZC_CONFIG()`) — c'est l'ESP32 qui forme le réseau, les têtes s'y joignent en tant qu'End Device
* Radio native 802.15.4 de l'ESP32-C6 (`ZB_RADIO_MODE_NATIVE`), pas de co-processeur radio externe
* Canal fixé par compilation (25 par défaut) — pas de changement de canal à chaud sans reflash pour l'instant
* Endpoint 1, profil HA (`ESP_ZB_AF_HA_PROFILE_ID`), device ID thermostat (`ESP_ZB_HA_THERMOSTAT_DEVICE_ID`)
* Clusters exposés : Basic (`0x0000`), Identify, Power Config, Thermostat (`0x0201` — présent pour la conformité au profil HA mais non réellement câblé côté firmware Tuya des têtes, aucune donnée ne transite dessus), et le cluster propriétaire Tuya `0xEF00` en double rôle CLIENT+SERVER

### Appairage
* Le réseau Zigbee est **fermé par défaut**, y compris au démarrage de l'ESP32 — contrairement au prototype, qui rouvrait automatiquement le réseau 180s à chaque boot (`esp_zb_bdb_open_network(180)` appelé systématiquement dans `esp_zb_app_signal_handler()`). Ici, l'ouverture n'a lieu que sur réception explicite d'une commande UART `permit_join` envoyée par la RPI (durée paramétrable, ex. 180s)
* Avant même de traiter le réseau Zigbee, l'ESP32 démarre par une phase de synchronisation avec la RPI : il envoie `known_devices_request` et attend la réponse `known_devices_list` pour reconstruire en mémoire sa table des devices déjà connus (adresse courte, adresse IEEE, fabricant, modèle) — la RPI est la source de vérité persistée, l'ESP32 ne conserve rien en flash de son côté
* Un nouveau device est détecté via le signal `ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE` (uniquement possible pendant une fenêtre `permit_join` ouverte), puis identifié par lecture ZCL standard du cluster Basic (« magic packet » : `manufacturerName`, `modelIdentifier`, `zclVersion`, `powerSource`...) — seule méthode d'identification fiable observée, le cluster Thermostat ne répondant pas
* Le device est ajouté à la table en mémoire, et un message UART `pairing_request` est émis vers la RPI (adresse courte, adresse IEEE, fabricant, modèle) ; c'est à la RPI de le persister dans sa propre liste de devices connus
* La RPI peut à tout moment révoquer l'appairage d'un device via `remove_device` (`short_addr`) : l'ESP32 le fait quitter le réseau Zigbee (ZDO Leave) et le retire de sa table en mémoire, puis répond par un `command_ack`

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
* Réseau fermé par défaut au démarrage (le prototype ouvre systématiquement le réseau 180s à chaque boot — comportement à retirer au profit d'une ouverture uniquement sur `permit_join`)
* Échange `known_devices_request`/`known_devices_list` au démarrage, pour reconstruire la table en mémoire depuis la RPI (le prototype n'a pas ce mécanisme — sa table `g_paired_devices[]` est en RAM et repart vide à chaque reboot, sans aucune synchronisation)

La cible du projet est de créer une gateway ZigBee sur une ESP32-C6-ZERO qui sera capable d'interagir avec une tête thermostatique programable KETOTEK KTF0177

* le répertoire /gateway contient le projet ESP-IDF de la gateway
* le  repertoire /thermostat contient le projet ESP-IDF d'une simulation du thermostat tournant sur une autre ESP32-C6-ZERO

Le code doit permettre l'appairage des devices, la lecture des données du thermostat et la reprogramation du thermostat

# Scenario de test

## Cas simple

Détail des éléments du protocole Zigbee utilisés à chaque étape (implémentation dans `gateway/main/esp_zigbee_gateway.c` et `thermostat/main/esp_zigbee_thermostat_sim.c`) :

* **La gateway démarre**
  - Rôle Zigbee : Coordinateur (`ESP_ZB_DEVICE_TYPE_COORDINATOR`, `CONFIG_ZB_ZCZR=y`, macro `ESP_ZB_ZC_CONFIG()`)
  - Commissioning BDB : si factory-new → formation réseau (`ESP_ZB_BDB_MODE_NETWORK_FORMATION`, signal `ESP_ZB_BDB_SIGNAL_FORMATION`) puis ouverture du réseau 180s (`esp_zb_bdb_open_network(180)`) et démarrage du steering (`ESP_ZB_BDB_MODE_NETWORK_STEERING`)
  - Canal fixé par `CONFIG_THERMOSTAT_DEFAULT_CHANNEL` (25 par défaut)
  - Implémenté : `esp_zb_app_signal_handler()` (gateway)

* **Le thermostat démarre**
  - Rôle Zigbee : End Device (`ESP_ZB_DEVICE_TYPE_ED`, `CONFIG_ZB_ZED=y`, macro `ESP_ZB_ZED_CONFIG()`)
  - Commissioning BDB : si factory-new → recherche d'un réseau ouvert (`ESP_ZB_BDB_MODE_NETWORK_STEERING` uniquement — un end device ne forme jamais de réseau)
  - Balaie tous les canaux (`ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK`), pas de canal fixe
  - Implémenté : `esp_zb_app_signal_handler()` (thermostat)

* **Le thermostat s'appaire à la gateway**
  - Côté thermostat : signal `ESP_ZB_BDB_SIGNAL_STEERING` réussi → démarrage du timer de rapport périodique (`start_report_timer()`)
  - Côté gateway : signal `ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE` → enregistrement du device dans `g_paired_devices[]`
  - Les deux projets utilisent l'endpoint 1, profil HA (`ESP_ZB_AF_HA_PROFILE_ID`), device ID `ESP_ZB_HA_THERMOSTAT_DEVICE_ID`, et exposent les mêmes clusters (Basic, Identify, Power Config, Thermostat 0x0201, Tuya 0xEF00 en double rôle CLIENT+SERVER)
  - Implémenté des deux côtés — **non encore testé sur les 2 cartes physiques**

* **Le thermostat envoie son statut (température) à la gateway**
  - Protocole : cluster Tuya propriétaire `0xEF00`, commande `TUYA_CMD_REPORT_1 (0x01)`, DataPoints DP101 (`KETOTEK_DP_SYSTEM_STATE`, bool) + DP102 (`KETOTEK_DP_LOCAL_TEMP`, valeur ×10) + DP103 (`KETOTEK_DP_HEATING_SETPOINT`, valeur ×10)
  - Implémenté : `tuya_send_dp_report()` (thermostat), déclenché automatiquement toutes les `CONFIG_THERMOSTAT_SIM_REPORT_INTERVAL_SEC` secondes (30s par défaut) après appairage, via `esp_zb_scheduler_alarm()`
  - Réception côté gateway : `zb_action_handler()` / `ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID` → `tuya_log_dp()` (log uniquement pour l'instant, pas d'état consultable en dehors des logs)
  - ⚠️ À vérifier sur matériel réel : la direction ZCL `ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI` utilisée par le thermostat pour ces rapports (jamais testée contre le rôle CLIENT du cluster Tuya de la gateway — voir TODO dans le code)

* **La gateway envoie la température de consigne au thermostat**
  - Protocole : cluster Tuya `0xEF00`, commande `TUYA_CMD_SET_DATA (0x00)`, DataPoint DP103 (`KETOTEK_DP_HEATING_SETPOINT`), type `TUYA_DP_TYPE_VALUE`, valeur encodée ×10 big-endian
  - Implémenté : `tuya_send_set_temperature()` (gateway), déclenchée automatiquement (`g_awaiting_setpoint_addr`) dès réception du **premier** rapport de statut Tuya du device qui vient de s'appairer — pas immédiatement au `DEVICE_ANNCE` (le device n'est pas forcément prêt à recevoir une commande à cet instant), et en une seule fois (pas de boucle périodique, contrairement au hack retiré d'`old/`). Valeur de démo : `DEMO_HEATING_SETPOINT_DEG` (21°C)
  - Réception côté thermostat : `handle_tuya_set_data()` (thermostat) — déjà implémentée, met à jour `g_occupied_heating_setpoint`

### Reste à faire pour exécuter ce scénario de bout en bout

1. ~~Déclencher `tuya_send_set_temperature()` côté gateway après appairage~~ — fait : envoi automatique dès le premier rapport de statut reçu (voir ci-dessus)
2. Flasher les deux cartes (`idf.py -p <port> flash monitor` dans `gateway/` et `thermostat/`) et valider dans les logs, dans l'ordre : formation réseau (gateway) → steering + join (thermostat) → device announce (gateway) → rapport DP101/102/103 reçu par la gateway → setpoint DP103 envoyé par la gateway → reçu et appliqué par le thermostat
3. Vérifier la direction ZCL des rapports du thermostat (`TO_CLI`) une fois le round-trip testé — corriger en `TO_SRV` si la gateway ne les reçoit pas correctement
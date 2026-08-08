La cible du projet est de créer une gateway ZigBee sur une ESP32-C6-ZERO qui sera capable d'interagir avec une tête thermostatique programable KETOTEK KTF0177

* le répertoire /gateway contient le projet ESP-IDF de la gateway
* le  repertoire /thermostat contient le projet ESP-IDF d'une simulation du thermostat tournant sur une autre ESP32-C6-ZERO

Le code doit permettre l'appairage des devices, la lecture des données du thermostat et la reprogramation du thermostat

# Scenario de test

## Cas simple

✅ **Scénario validé de bout en bout sur les deux cartes physiques** (2026-08-08).

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
  - ✅ Validé sur les 2 cartes physiques

* **Le thermostat envoie son statut (température) à la gateway**
  - Protocole : cluster Tuya propriétaire `0xEF00`, commande `TUYA_CMD_REPORT_1 (0x01)`, DataPoints DP101 (`KETOTEK_DP_SYSTEM_STATE`, bool) + DP102 (`KETOTEK_DP_LOCAL_TEMP`, valeur ×10) + DP103 (`KETOTEK_DP_HEATING_SETPOINT`, valeur ×10)
  - Implémenté : `tuya_send_dp_report()` (thermostat), déclenché automatiquement toutes les `CONFIG_THERMOSTAT_SIM_REPORT_INTERVAL_SEC` secondes (30s par défaut) après appairage, via `esp_zb_scheduler_alarm()`
  - Réception côté gateway : `zb_action_handler()` / `ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID` (et `_RESP_CB_ID`) → `tuya_log_dp()` (log uniquement pour l'instant, pas d'état consultable en dehors des logs)
  - ✅ Validé sur les 2 cartes physiques — direction ZCL `TO_SRV` des deux côtés, type de donnée `ESP_ZB_ZCL_ATTR_TYPE_SET` (voir note sur le bug ARRAY ci-dessous)

* **La gateway envoie la température de consigne au thermostat**
  - Protocole : cluster Tuya `0xEF00`, commande `TUYA_CMD_SET_DATA (0x00)`, DataPoint DP103 (`KETOTEK_DP_HEATING_SETPOINT`), type `TUYA_DP_TYPE_VALUE`, valeur encodée ×10 big-endian
  - Implémenté : `tuya_send_set_temperature()` (gateway), déclenchée automatiquement (`g_demo_setpoint_sent`) dès réception du **premier** rapport de statut Tuya reçu depuis le démarrage de la gateway — pas immédiatement au `DEVICE_ANNCE` (le device n'est pas forcément prêt à recevoir une commande à cet instant, et un device déjà appairé qui redémarre simplement ne renvoie pas toujours ce signal), et en une seule fois (pas de boucle périodique, contrairement au hack retiré d'`old/`). Valeur de démo : `DEMO_HEATING_SETPOINT_DEG` (21°C)
  - Réception côté thermostat : `handle_tuya_set_data()` (thermostat) — met à jour `g_occupied_heating_setpoint`
  - ✅ Validé sur les 2 cartes physiques

### Bug résolu pendant la validation matérielle

Le round-trip DP103 est resté silencieux (aucune erreur, mais rien reçu) pendant plusieurs itérations de debug. Cause racine trouvée via `esp_zb_set_trace_level_mask()` (traces bas-niveau APS) : les 4 envois Tuya (gateway ×2, thermostat ×2) utilisaient `.type = ESP_ZB_ZCL_ATTR_TYPE_ARRAY`, qui impose que les 2 premiers octets du buffer soient un préfixe de comptage réinterprété par la pile (`size = 2 + contenu`). Nos 2 premiers octets (numéro de séquence Tuya) étaient donc lus comme un nombre d'éléments, gonflant la taille de trame calculée jusqu'à ce que la couche NWK la rejette silencieusement ("Seems too big data frame - do not send it"). Remplacé par `ESP_ZB_ZCL_ATTR_TYPE_SET` (taille = nombre d'octets brut, pas de préfixe) des deux côtés — corrige le problème.

### Reste à faire (au-delà du "Cas simple")

- CLI console (`permit_join`/`list_devices`/`remove_device`) — toujours un `TODO(THERMOSTAT_ENABLE_CLI)`, non implémentée
- Écriture du planning hebdomadaire (DP 0x1C-0x22 / 109 / 123-129) — actuellement décodée en lecture seule (log), pas d'écriture
- Réponse au Tuya Time Sync Request (cmd 0x24) — non implémentée
- Persistance NVS des devices appairés côté gateway (`g_paired_devices` est en RAM, perdu au reboot)
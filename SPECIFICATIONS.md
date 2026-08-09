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
  - Implémenté : `tuya_send_set_temperature()` (gateway), déclenchée automatiquement (`g_demo_setpoint_sent`) dès réception du **premier** rapport de statut Tuya reçu depuis le démarrage de la gateway — pas immédiatement au `DEVICE_ANNCE` (le device n'est pas forcément prêt à recevoir une commande à cet instant, et un device déjà appairé qui redémarre simplement ne renvoie pas toujours ce signal), et en une seule fois (pas de boucle périodique, contrairement au hack retiré d'`old/`). Valeur de démo : `DEMO_HEATING_SETPOINT_DEG` (14°C)
  - Réception côté thermostat : `handle_tuya_set_data()` (thermostat) — met à jour `g_occupied_heating_setpoint`
  - ✅ Validé sur les 2 cartes physiques

### Bug résolu pendant la validation matérielle

Le round-trip DP103 est resté silencieux (aucune erreur, mais rien reçu) pendant plusieurs itérations de debug. Cause racine trouvée via `esp_zb_set_trace_level_mask()` (traces bas-niveau APS) : les 4 envois Tuya (gateway ×2, thermostat ×2) utilisaient `.type = ESP_ZB_ZCL_ATTR_TYPE_ARRAY`, qui impose que les 2 premiers octets du buffer soient un préfixe de comptage réinterprété par la pile (`size = 2 + contenu`). Nos 2 premiers octets (numéro de séquence Tuya) étaient donc lus comme un nombre d'éléments, gonflant la taille de trame calculée jusqu'à ce que la couche NWK la rejette silencieusement ("Seems too big data frame - do not send it"). Remplacé par `ESP_ZB_ZCL_ATTR_TYPE_SET` (taille = nombre d'octets brut, pas de préfixe) des deux côtés — corrige le problème.

### Reste à faire (au-delà du "Cas simple")

- CLI console (`permit_join`/`list_devices`/`remove_device`) — toujours un `TODO(THERMOSTAT_ENABLE_CLI)`, non implémentée
- Écriture du planning hebdomadaire (DP 0x1C-0x22 / 109 / 123-129) — actuellement décodée en lecture seule (log), pas d'écriture
- Réponse au Tuya Time Sync Request (cmd 0x24) — non implémentée
- Persistance NVS des devices appairés côté gateway (`g_paired_devices` est en RAM, perdu au reboot)

## Cas reel

⬜ **Non encore exécuté** — procédure à suivre pour rejouer le "Cas simple" ci-dessus avec le vrai KETOTEK KTF0177 physique à la place du firmware `/thermostat` (simulateur). Mettre à jour ce paragraphe (✅ + logs) une fois le test réalisé.

### Ce qui change par rapport au "Cas simple"

* Un seul ESP32-C6 à flasher : la gateway (`/gateway`). Le KTF0177 est un appareil Tuya fermé, on ne flashe rien dessus — il rejoint le réseau avec son propre firmware.
* Pas de simulateur = pas de bouton logiciel : l'appairage et le comportement du device dépendent du vrai firmware Tuya du KTF0177 (bouton physique, écran, capteur de température réel).
* Le KTF0177 peut exposer plus de DataPoints que le simulateur (`/thermostat` n'implémente que DP101/102/103) : DP3 (`KETOTEK_DP_HEATING_STATE`), DP8 (`WINDOW_DETECTION`), DP10 (`FROST_DETECTION`), DP27 (`TEMP_CALIBRATION`), DP40 (`CHILD_LOCK`), DP104 (`VALVE_POSITION`), DP105 (`BATTERY_LOW`), DP106 (`AWAY_MODE`), DP107/108 (`SCHEDULE_MODE`/`ENABLE`), DP109+123-129 (planning hebdomadaire). Tous déjà décodés en lecture seule par `tuya_log_dp()` (`gateway/main/esp_zigbee_gateway.c`), donc pas de comportement inattendu prévu — juste plus de lignes de log.

### Procédure

1. **Flasher et monitorer la gateway seule** : `idf.py -p <port> flash monitor` dans `/gateway`. Vérifier la formation du réseau (`ESP_ZB_BDB_SIGNAL_FORMATION`) et le canal utilisé (`CONFIG_THERMOSTAT_DEFAULT_CHANNEL`, 25 par défaut).
2. **Mettre le KTF0177 en mode appairage** : suivre la procédure du fabricant (généralement appui long sur le bouton de la tête thermostatique jusqu'à ce que l'écran/LED indique le mode pairing). Aucune commande CLI n'est nécessaire côté gateway : `esp_zb_app_signal_handler()` rouvre automatiquement le réseau 180s à **chaque démarrage** de la gateway (factory-new ou reboot simple), voir `esp_zb_bdb_open_network(180)` aux deux endroits du handler.
3. **Vérifier l'appairage** dans les logs gateway : signal `ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE` puis `Device REGISTERED! Total paired devices: N`.
4. **Vérifier la réception du premier rapport Tuya** : ligne `Réception DP report` / sortie de `tuya_log_dp()` avec au minimum DP101 (état), DP102 (température locale), DP103 (consigne) — comparer aux valeurs affichées sur l'écran physique du KTF0177.
5. **Vérifier l'envoi de la consigne de démo** : dès ce premier rapport reçu, la gateway envoie automatiquement `DEMO_HEATING_SETPOINT_DEG` (14°C) via `tuya_send_set_temperature()`. Confirmer sur l'écran du KTF0177 que la consigne affichée passe à 14°C.

### Points de vigilance spécifiques au matériel réel

* **Pas de CLI encore implémentée** (`TODO(THERMOSTAT_ENABLE_CLI)`) : pour rouvrir une fenêtre d'appairage à la demande, il suffit de redémarrer la gateway (bouton EN ou re-power) — pas besoin d'un `permit_join` manuel. Pour un vrai reset du réseau (reformation complète), effacer la flash : `idf.py -p <port> erase-flash` puis reflasher.
* **Canal Zigbee** : la gateway fixe le canal 25 par `CONFIG_THERMOSTAT_DEFAULT_CHANNEL` (define compilé, pas un réglage runtime — nécessite un rebuild pour changer). Le KTF0177, en tant qu'End Device, scanne normalement tous les canaux au moment du join, donc ça doit passer ; en cas d'échec d'appairage répété, essayer un canal moins encombré (11, 15, 20) et reflasher.
* **Time Sync Request (cmd 0x24)** non géré côté gateway (TODO) : si le KTF0177 en émet un après appairage et ne reçoit pas de réponse, surveiller les logs pour un éventuel comportement de re-synchronisation en boucle.
* **Écriture du planning hebdomadaire** non implémentée (lecture seule) : ne pas s'attendre à pouvoir reprogrammer le planning du KTF0177 depuis la gateway à ce stade.
* **Portée radio réelle** : contrairement au banc de test avec deux cartes posées côte à côte, prévoir une distance raisonnable dès le premier essai (quelques mètres) puis éloigner progressivement pour valider la portée.
* **Cadence des rapports** : le timer `CONFIG_THERMOSTAT_SIM_REPORT_INTERVAL_SEC` n'existe que côté simulateur — le vrai KTF0177 rapporte son statut selon la logique de son propre firmware Tuya (pas nécessairement toutes les 30s).

# Protocole

Éléments du protocole Tuya identifiés à ce jour — table de référence, complète les détails déjà donnés au fil du "Scenario de test". Implémentation partagée dans `gateway/main/tuya_ketotek_dp.h` et sa copie identique `thermostat/main/tuya_ketotek_dp.h` (garder les deux synchronisées).

## Format général (cluster Tuya `0xEF00`)

* Cluster propriétaire Tuya `0xEF00`, exposé en double rôle CLIENT+SERVER, endpoint 1, profil HA — des deux côtés (gateway et device).
* Trame identique en émission et réception : `[SEQ_H][SEQ_L][DP_ID][DP_TYPE][LEN_H][LEN_L][DATA...]` (`TUYA_DP_HEADER_LEN = 6`, décodage/encodage via `tuya_dp_decode_value()`/`tuya_dp_encode_value()`).
* Toutes les valeurs multi-octets sont en big-endian. Les températures sont encodées ×10 (ex: 14.0°C → 140).
* Envoi bas niveau : `.type = ESP_ZB_ZCL_ATTR_TYPE_SET` (pas `ARRAY`, voir bug résolu dans "Cas simple").

### Commandes du cluster (`custom_cmd_id`)

| Cmd | Constante | Direction | Usage observé |
|---|---|---|---|
| `0x00` | `TUYA_CMD_SET_DATA` | gateway → device | Écrire un DP (ex: consigne DP103) |
| `0x01` | `TUYA_CMD_REPORT_1` | device → gateway | Rapport spontané (utilisé par le simulateur `/thermostat`) |
| `0x02` | `TUYA_CMD_REPORT_2` | device → gateway | Rapport spontané observé sur le vrai KTF0177 |
| `0x11` | `TUYA_CMD_QUERY` | gateway → device | Requête "tous les DP" (implémentée côté envoi, jamais testée) |
| `0x24` | `TUYA_CMD_TIME_SYNC` | device → gateway | Requête de synchro horaire — reçue mais non traitée (TODO) |

### Types de données (`DP_TYPE`)

`0x01` Bool (1 byte) · `0x02` Value (int32 big-endian, 4 bytes) · `0x03` String · `0x04` Enum (1 byte) · `0x05` Bitmap.

## DataPoints confirmés (table Saswell/KETOTEK)

Repris du converter Saswell de `zigbee-herdsman-converters` (voir `old/ZIGBEE2MQTT_ANALYSIS.md`) — **pas** de l'ancien guess `old/TUYA_ZIGBEE_PROTOCOL.md`, qui utilisait un mapping DP différent et erroné.

| DP | Constante | Type | Sens | Statut |
|---|---|---|---|---|
| 3 | `KETOTEK_DP_HEATING_STATE` | Enum | État de chauffe | Décodé (log), non observé sur le réel |
| 8 | `KETOTEK_DP_WINDOW_DETECTION` | Bool | Détection fenêtre ouverte | Décodé (log), non observé sur le réel |
| 10 | `KETOTEK_DP_FROST_DETECTION` | Bool | Protection hors-gel | Décodé (log), non observé sur le réel |
| 27 | `KETOTEK_DP_TEMP_CALIBRATION` | Value, signé | Calibration sonde (-6..+6°C) | Décodé (log), non observé sur le réel |
| 40 | `KETOTEK_DP_CHILD_LOCK` | Bool | Verrouillage enfant | Décodé (log), non observé sur le réel |
| 101 | `KETOTEK_DP_SYSTEM_STATE` | Bool | Marche/arrêt | ✅ Envoyé par le simulateur, à confirmer sur le réel |
| 102 | `KETOTEK_DP_LOCAL_TEMP` | Value ×10°C | Température mesurée | ✅ Envoyé par le simulateur, à confirmer sur le réel |
| 103 | `KETOTEK_DP_HEATING_SETPOINT` | Value ×10°C | Consigne de chauffe | ✅ Round-trip gateway↔device validé (simulateur **et** réel, voir "Cas reel") |
| 104 | `KETOTEK_DP_VALVE_POSITION` | Value, 0-100% | Position de vanne | Décodé (log), non observé sur le réel |
| 105 | `KETOTEK_DP_BATTERY_LOW` | Bool | Pile faible | Décodé (log), non observé sur le réel |
| 106 | `KETOTEK_DP_AWAY_MODE` | Bool | Mode absence | Décodé (log), non observé sur le réel |
| 107 | `KETOTEK_DP_SCHEDULE_MODE` | Enum | Mode planning | Décodé (log), non observé sur le réel |
| 108 | `KETOTEK_DP_SCHEDULE_ENABLE` | Bool | Activation planning | Décodé (log), non observé sur le réel |
| 109, 123-129 | `KETOTEK_DP_WEEKLY_SCHEDULE_BASE` | Raw | Planning hebdomadaire (7 jours) | Décodé en octets bruts uniquement, pas de parsing ni d'écriture |

## DataPoints identifiés sur le matériel réel (absents de la table Saswell)

Observés en conditions réelles avec le KTF0177 physique (short addr `0x3d6e`, 2026-08-09), non documentés dans le converter Saswell :

* **DP4 — `KETOTEK_DP_HEATING_SETPOINT_ECHO`** (Value ×10°C) : **confirmé**. Envoie la consigne actuellement appliquée sur la tête. Validé en deux temps : (1) après un envoi gateway→device de 14°C (DP103), la valeur de DP4 a convergé progressivement vers `140` (14.0°C) sur plusieurs rapports successifs ; (2) confirmé de façon décisive en changeant la consigne **manuellement sur la tête elle-même** — DP4 a suivi ce changement, prouvant qu'il s'agit bien d'un miroir de la consigne appliquée (et non d'une mesure de température ambiante ou d'une position de vanne). Décodé dans `tuya_log_dp()`.
* **DP2 — `KETOTEK_DP_CONTROL_MODE`** (Enum, 1 byte) : **confirmé**. Mode Manuel/Automatique de la tête, basculé par le bouton central — doc constructeur : *"Temperature Control Mode: Switch between manual and automatic modes by pressing the middle button."* Sens des valeurs confirmé par observation directe de l'écran de la tête : **`0` = Automatique**, **`1` = Manuel**. Décodé dans `tuya_log_dp()`.
* **DP7 — `KETOTEK_DP_CHILD_LOCK_REAL`** (Bool, 1 byte) : **confirmé**. Verrouillage enfant — confirmé en activant puis désactivant le verrouillage sur la tête et en observant DP7 passer `1`(ON) → `0`(OFF) en conséquence. La table Saswell/KETOTEK documente le child lock sur **DP40** (`KETOTEK_DP_CHILD_LOCK`), jamais observé sur ce device réel ; c'est en fait **DP7** qui est utilisé, comme le devinait (correctement, cette fois) `old/TUYA_ZIGBEE_PROTOCOL.md`. Décodé dans `tuya_log_dp()`.
* **DP5 — `KETOTEK_DP_LOCAL_TEMP_REAL`** (Value ×10°C) : **confirmé**. Température locale mesurée — deux relevés successifs (23.0°C puis 24.0°C, montée progressive cohérente avec une mesure réelle) confirmés identiques à la valeur affichée sur l'écran de la tête. La table Saswell/KETOTEK documente la température locale sur **DP102** (`KETOTEK_DP_LOCAL_TEMP`), jamais observé sur ce device réel ; c'est en fait **DP5** qui est utilisé, comme le devinait `old/TUYA_ZIGBEE_PROTOCOL.md`. Décodé dans `tuya_log_dp()`.

## Notes historiques

* `old/TUYA_ZIGBEE_PROTOCOL.md` documente un mapping DP différent (DP02 = température, DP04 = consigne **ou** état de chauffe selon le type) reverse-engineered avant d'avoir la vraie tête — **ne pas s'y fier**, gardé pour archive uniquement. Les DP2/DP4 réels observés ci-dessus ne correspondent pas à ce mapping.
* Bug ARRAY vs SET (`ESP_ZB_ZCL_ATTR_TYPE_ARRAY` cassant l'envoi) : voir "Cas simple" → "Bug résolu pendant la validation matérielle".

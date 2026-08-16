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
  - Protocole : cluster Tuya propriétaire `0xEF00`, commande `TUYA_CMD_REPORT_1 (0x01)`, DataPoints DP2 (`KETOTEK_DP_SYSTEM_MODE`, enum) + DP7 (`KETOTEK_DP_CHILD_LOCK`, bool) + DP5 (`KETOTEK_DP_LOCAL_TEMP`, valeur ×10) + DP4 (`KETOTEK_DP_HEATING_SETPOINT`, valeur ×10) — table réelle du device (AVATTO ME167_1 / TS0601_thermostat_5, voir "Protocole"), pas la table Saswell d'origine (DP101/102/103) utilisée à la validation initiale du 2026-08-08
  - Implémenté : `tuya_send_dp_report()` (thermostat), déclenché automatiquement toutes les `CONFIG_THERMOSTAT_SIM_REPORT_INTERVAL_SEC` secondes (30s par défaut) après appairage, via `esp_zb_scheduler_alarm()`
  - Réception côté gateway : `zb_action_handler()` / `ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID` (et `_RESP_CB_ID`) → `tuya_log_dp()` (log uniquement pour l'instant, pas d'état consultable en dehors des logs)
  - ✅ Validé sur les 2 cartes physiques — direction ZCL `TO_SRV` des deux côtés, type de donnée `ESP_ZB_ZCL_ATTR_TYPE_SET` (voir note sur le bug ARRAY ci-dessous)

* **La gateway envoie la température de consigne au thermostat**
  - Protocole : cluster Tuya `0xEF00`, commande `TUYA_CMD_SET_DATA (0x00)`, DataPoint DP4 (`KETOTEK_DP_HEATING_SETPOINT`), type `TUYA_DP_TYPE_VALUE`, valeur encodée ×10 big-endian
  - Implémenté : `tuya_send_set_temperature()` (gateway), déclenchée automatiquement (`demo_setpoint_already_sent()`/`demo_setpoint_mark_sent()`) dès réception du **premier** rapport de statut Tuya reçu depuis le démarrage de la gateway, **par device** — pas immédiatement au `DEVICE_ANNCE` (le device n'est pas forcément prêt à recevoir une commande à cet instant, et un device déjà appairé qui redémarre simplement ne renvoie pas toujours ce signal), et une seule fois par device (pas de boucle périodique, contrairement au hack retiré d'`old/`). Valeur de démo : `DEMO_HEATING_SETPOINT_DEG` (14°C)
  - ⚠️ Historique : c'était initialement un flag global unique (`g_demo_setpoint_sent`) plutôt que par device — avec le vrai KETOTEK et le simulateur `/thermostat` appairés simultanément, le premier des deux à rapporter après un boot de la gateway "gagnait" la consigne de démo et l'autre ne la recevait jamais (le simulateur, avec son rythme fixe 30s, battait systématiquement le vrai device). Corrigé en trackant l'envoi par `short_addr`.
  - Réception côté thermostat : `handle_tuya_set_data()` (thermostat) — met à jour `g_occupied_heating_setpoint`
  - ✅ Validé sur les 2 cartes physiques

### Bug résolu pendant la validation matérielle

Le round-trip DP103 est resté silencieux (aucune erreur, mais rien reçu) pendant plusieurs itérations de debug. Cause racine trouvée via `esp_zb_set_trace_level_mask()` (traces bas-niveau APS) : les 4 envois Tuya (gateway ×2, thermostat ×2) utilisaient `.type = ESP_ZB_ZCL_ATTR_TYPE_ARRAY`, qui impose que les 2 premiers octets du buffer soient un préfixe de comptage réinterprété par la pile (`size = 2 + contenu`). Nos 2 premiers octets (numéro de séquence Tuya) étaient donc lus comme un nombre d'éléments, gonflant la taille de trame calculée jusqu'à ce que la couche NWK la rejette silencieusement ("Seems too big data frame - do not send it"). Remplacé par `ESP_ZB_ZCL_ATTR_TYPE_SET` (taille = nombre d'octets brut, pas de préfixe) des deux côtés — corrige le problème.

### Reste à faire (au-delà du "Cas simple")

- CLI console (`permit_join`/`list_devices`/`remove_device`) — toujours un `TODO(THERMOSTAT_ENABLE_CLI)`, non implémentée
- Écriture du planning hebdomadaire (DP 28-34) — actuellement décodée en lecture seule (log), pas d'écriture
- Réponse au Tuya Time Sync Request (cmd 0x24) — non implémentée
- Persistance NVS des devices appairés côté gateway (`g_paired_devices` est en RAM, perdu au reboot)

## Cas reel

✅ **Lecture et écriture confirmées de bout en bout sur le vrai KTF0177 physique** (2026-08-09, short addr `0x3d6e`) — appairage, lecture DP2/DP5/DP7, et écriture de la consigne sur DP4 avec changement effectif visible sur l'écran de la tête. Procédure ci-dessous conservée telle qu'écrite avant exécution.

### Ce qui change par rapport au "Cas simple"

* Un seul ESP32-C6 à flasher : la gateway (`/gateway`). Le KTF0177 est un appareil Tuya fermé, on ne flashe rien dessus — il rejoint le réseau avec son propre firmware.
* Pas de simulateur = pas de bouton logiciel : l'appairage et le comportement du device dépendent du vrai firmware Tuya du KTF0177 (bouton physique, écran, capteur de température réel).
* Le KTF0177 peut exposer plus de DataPoints que le simulateur (`/thermostat` envoie DP2/DP4/DP5/DP7) : DP3 (`KETOTEK_DP_RUNNING_STATE`), DP28-34 (planning hebdomadaire), DP35 (`ERROR_OR_BATTERY_LOW`), DP36 (`FROST_PROTECTION`), DP39 (`SCALE_PROTECTION`), DP47 (`LOCAL_TEMP_CALIBRATION`), DP101 (`PI_HEATING_DEMAND`). Tous déjà décodés en lecture seule par `tuya_log_dp()` (`gateway/main/esp_zigbee_gateway.c`), donc pas de comportement inattendu prévu — juste plus de lignes de log.

### Procédure

1. **Flasher et monitorer la gateway seule** : `idf.py -p <port> flash monitor` dans `/gateway`. Vérifier la formation du réseau (`ESP_ZB_BDB_SIGNAL_FORMATION`) et le canal utilisé (`CONFIG_THERMOSTAT_DEFAULT_CHANNEL`, 25 par défaut).
2. **Mettre le KTF0177 en mode appairage** : suivre la procédure du fabricant (généralement appui long sur le bouton de la tête thermostatique jusqu'à ce que l'écran/LED indique le mode pairing). Aucune commande CLI n'est nécessaire côté gateway : `esp_zb_app_signal_handler()` rouvre automatiquement le réseau 180s à **chaque démarrage** de la gateway (factory-new ou reboot simple), voir `esp_zb_bdb_open_network(180)` aux deux endroits du handler.
3. **Vérifier l'appairage** dans les logs gateway : signal `ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE` puis `Device REGISTERED! Total paired devices: N`.
4. **Vérifier la réception du premier rapport Tuya** : ligne `Réception DP report` / sortie de `tuya_log_dp()` avec au minimum DP2 (mode), DP5 (température locale), DP4 (consigne) — comparer aux valeurs affichées sur l'écran physique du KTF0177.
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

## Identification du device

Le KETOTEK KTF0177 réel (short addr `0x3d6e`) s'identifie, via lecture ZCL standard du cluster Basic (`0x0000`, voir "Magic packet" plus bas) :
* **`manufacturerName` = `_TZE200_p3dbf6qs`**
* **`modelIdentifier` = `TS0601`**

Ce fingerprint correspond **exactement** à une entrée de `zigbee-herdsman-converters` (`old/documentation/zigbee-herdsman-converters/src/devices/tuya.ts`) : modèle **`TS0601_thermostat_5`**, marque blanche **AVATTO ME167_1**. Autrement dit, cette tête vendue/rebadgée "KETOTEK KTF0177" est en réalité un **AVATTO ME167_1** — **pas** un device de la famille Saswell (SEA801/SEA802) supposée par similarité jusqu'ici. Le converter `tuya.ts` correspondant fournit une table `meta.tuyaDatapoints` faisant autorité, utilisée ci-dessous à la place de l'ancienne table Saswell (voir "Notes historiques").

## Format général (cluster Tuya `0xEF00`)

* Cluster propriétaire Tuya `0xEF00`, exposé en double rôle CLIENT+SERVER, endpoint 1, profil HA — des deux côtés (gateway et device).
* Trame identique en émission et réception : `[SEQ_H][SEQ_L][DP_ID][DP_TYPE][LEN_H][LEN_L][DATA...]` (`TUYA_DP_HEADER_LEN = 6`, décodage/encodage via `tuya_dp_decode_value()`/`tuya_dp_encode_value()`).
* Toutes les valeurs multi-octets sont en big-endian. Les températures sont encodées ×10 (ex: 14.0°C → 140).
* Envoi bas niveau : `.type = ESP_ZB_ZCL_ATTR_TYPE_SET` (pas `ARRAY`, voir bug résolu dans "Cas simple").

### Commandes du cluster (`custom_cmd_id`)

Table corrigée d'après la doc officielle Tuya (*Tuya Zigbee Universal Docking Access Standard*, developer.tuya.com/en/docs/iot/tuya-zigbee-universal-docking-access-standard?id=K9ik6zvofpzql) — voir "Erreur historique sur `0x11`" plus bas pour l'ancienne table erronée.

| Cmd | Constante | Nom officiel | Direction | Usage observé |
|---|---|---|---|---|
| `0x00` | `TUYA_CMD_SET_DATA` | `TY_DATA_REQUEST` | gateway → device | Écrire un DP (ex: consigne DP4) |
| `0x01` | `TUYA_CMD_REPORT_1` | `TY_DATA_RESPONE` | device → gateway | Réponse à une requête de données (utilisé par le simulateur `/thermostat` pour ses rapports) |
| `0x02` | `TUYA_CMD_REPORT_2` | `TY_DATA_REPORT` | device → gateway | Rapport spontané, bidirectionnel — observé sur le vrai KTF0177 |
| `0x03` | `TUYA_CMD_QUERY` | `TY_DATA_QUERY` | gateway → device | Requête "tous les DP", payload **vide** (pas même de numéro de séquence) — voir "Data Query" ci-dessous |
| `0x10` | `TUYA_CMD_MCU_VERSION_REQ` | `TUYA_MCU_VERSION_REQ` | gateway → device | Requête version firmware MCU — non utilisée |
| `0x11` | `TUYA_CMD_MCU_VERSION_RSP` | `TUYA_MCU_VERSION_RSP` | device → gateway | Réponse/rapport version firmware MCU — **PAS un data query**, voir plus bas |
| `0x24` | `TUYA_CMD_TIME_SYNC` | `TUYA_MCU_SYNC_TIME` | device → gateway | Requête de synchro horaire — reçue mais non traitée (TODO) |

### Types de données (`DP_TYPE`)

`0x01` Bool (1 byte) · `0x02` Value (int32 big-endian, 4 bytes) · `0x03` String · `0x04` Enum (1 byte) · `0x05` Bitmap.

### Erreur historique sur `0x11` — corrigée le 2026-08-09

Le code (et `old/TUYA_ZIGBEE_PROTOCOL.md`) utilisaient `0x11` comme "Data Query" avec un payload `[SEQ_H][SEQ_L][0x00]`. **C'était une erreur** : d'après la doc officielle Tuya ci-dessus, `0x11` est en fait `TUYA_MCU_VERSION_RSP` (rapport de version firmware MCU), sans aucun rapport avec une requête de données. La vraie commande "query all DPs" est **`0x03`** (`TY_DATA_QUERY`), avec un payload **entièrement vide** ("*does not contain a ZCL payload*").

Ça explique complètement le comportement observé sur le matériel réel : la tête acquittait poliment une trame ZCL bien formée (`Default Response status=SUCCESS`) sans jamais renvoyer de données, parce qu'elle ne recevait tout simplement pas la bonne commande — `0x11` n'a jamais eu vocation à déclencher un dump d'état.

Corrigé dans `tuya_ketotek_dp.h` (les deux copies) et `tuya_send_data_query()` (`gateway/main/esp_zigbee_gateway.c`) : commande `0x03`, payload vide (`.size = 0`).

**Retesté sur le matériel réel (2026-08-09) avec la commande corrigée : même résultat que `0x11`** — `ZCL Default Response status=SUCCESS`, aucune donnée ne suit, déclenché sur DP7. La correction de commande n'a donc pas débloqué de dump.

**Conclusion révisée et renforcée** : ce n'était pas un problème de mauvaise commande — recoupé avec l'observation indépendante sur zigbee2mqtt (ni le converter Saswell, ni le vrai converter de ce device - AVATTO ME167_1/TS0601_thermostat_5, voir "Identification du device" - n'utilisent `dataQuery`), tout indique que **ce firmware MCU n'implémente simplement pas la fonctionnalité "query" côté device**, même si la commande existe dans la spec Tuya générique. Piste `0x03` fermée. Reste à tenter, si besoin : magic packet (lecture Basic cluster) et bind explicite (voir ci-dessous) — sinon, la seule source d'information fiable est les rapports spontanés DP par DP (cache d'état local, Option 1 déjà discutée).

### Piste explorée en parallèle : `configure()` de zigbee-herdsman-converters

Deux converters regardés dans `old/documentation/zigbee-herdsman-converters` : celui de la famille Saswell (similarité supposée au départ) et le vrai converter de ce device une fois identifié (voir "Identification du device" - AVATTO ME167_1 / TS0601_thermostat_5). **Aucun des deux n'active `queryOnConfigure` ni `queryOnDeviceAnnounce`** — ni l'un ni l'autre ne fait de data query :
* Saswell (`devices/saswell.ts`) : `tuya.modernExtend.tuyaBase({bindBasicOnConfigure: true, timeStart: "1970"})`.
* AVATTO ME167_1 / **le vrai converter de ce device** (`devices/tuya.ts`) : `tuya.modernExtend.tuyaBase({dp: true, timeStart: "2000"})` — pas de `bindBasicOnConfigure` non plus.

Dans les deux cas, `configureMagicPacket` reste actif par défaut (indépendant des options passées) : lecture ZCL standard du cluster **Basic (`0x0000`)** — `manufacturerName`, `zclVersion`, `appVersion`, `modelId`, `powerSource`, attribut `0xfffe`.

**Magic packet testé sur le réel (2026-08-09) : ✅ réponse complète, contrairement au cluster Thermostat.** `zcl_send_read_basic_attrs()` (`gateway/main/esp_zigbee_gateway.c`), déclenché sur DP5 :

| Attr | Nom | Type | Valeur |
|---|---|---|---|
| `0x0000` | zclVersion | U8 | `3` |
| `0x0001` | applicationVersion | U8 | `67` (`0x43`) |
| `0x0004` | manufacturerName | String | **`_TZE200_p3dbf6qs`** |
| `0x0005` | modelIdentifier | String | **`TS0601`** |
| `0x0007` | powerSource | Enum | `3` = Battery |
| `0xfffe` | (spécifique fabricant) | Enum | `0` |

Contrairement au cluster Thermostat `0x0201` (silence total, voir ci-dessous), **le cluster Basic `0x0000` répond bien avec de vraies données** — la tête n'ignore donc pas tout le ZCL standard, juste les clusters HA non réellement câblés sur son firmware Tuya (Thermostat, et `0x03`/Data Query côté cluster propriétaire). C'est précisément `manufacturerName`/`modelIdentifier` ci-dessus qui ont permis d'identifier le vrai modèle du device (voir "Identification du device" en tête de section).

**Bind explicite** (`zdo_bind_basic_cluster()`, `bindBasicOnConfigure`-style) : tenté mais **pas encore obtenu** — nécessite l'adresse IEEE du device, connue seulement après un `DEVICE_ANNCE` pendant le boot courant de la gateway (voir `find_paired_device_ieee()`), et les deux devices étaient déjà appairés lors des derniers tests (pas de nouveau `DEVICE_ANNCE`). Note : le vrai converter de ce device n'active de toute façon pas `bindBasicOnConfigure` (voir ci-dessus) — piste empruntée à la famille Saswell par analogie, pas confirmée nécessaire pour ce device précis. En attente d'un ré-appairage/re-announce pour retester.

### ZCL Read Attributes (cluster `0x0201`) — testé sur le réel, échec (silence)

Alternative tentée au Data Query Tuya : lecture standard ZCL `Read Attributes` sur le cluster Thermostat `0x0201` (`local_temperature`, `occupied_heating_setpoint`, `system_mode`), déclenchée en parallèle du Data Query à chaque rapport DP5 (`zcl_send_read_thermostat_attrs()`, `gateway/main/esp_zigbee_gateway.c`).

* **Aucune réponse** de la tête — ni `ZCL Read Attr Response` (`ESP_ZB_CORE_CMD_READ_ATTR_RESP_CB_ID`), ni même un `Default Response` d'erreur (confirmé en ajoutant le cluster au log du Default Response : seuls `0xef00`/`0x00` et `0xef00`/`0x11` reviennent, jamais `0x0201`).
* Conclusion : le cluster `0x0201` est probablement déclaré par la tête uniquement pour la compatibilité profil HA, sans être réellement câblé sur des données vivantes — le firmware Tuya ne répond que sur son cluster propriétaire `0xEF00`.
* Pas de piste de lecture "pull" fonctionnelle confirmée à ce jour côté ZCL standard (`0x0201`) — mais voir "Erreur historique sur `0x11`" plus haut : la commande Tuya `0x03` (`TY_DATA_QUERY`), pas encore testée avec le bon payload, reste une piste ouverte côté cluster `0xEF00`.

## DataPoints confirmés (AVATTO ME167_1 / TS0601_thermostat_5)

Table faisant autorité, reprise du converter `zigbee-herdsman-converters` correspondant **exactement** au fingerprint de ce device (`manufacturerName="_TZE200_p3dbf6qs"`, `modelIdentifier="TS0601"` — voir "Identification du device" en tête de section), `meta.tuyaDatapoints` dans `old/documentation/zigbee-herdsman-converters/src/devices/tuya.ts`. Remplace entièrement l'ancienne table Saswell/KETOTEK (DP101-109/123-129/DP40 etc.), qui ne s'applique pas à ce device précis — voir "Notes historiques".

| DP | Constante | Type | Sens | Statut |
|---|---|---|---|---|
| 2 | `KETOTEK_DP_SYSTEM_MODE` | Enum | `auto=0`, `heat=1`, `off=2` | ✅ Confirmé sur le réel — mode basculé par le bouton central de la tête (doc constructeur : *"Temperature Control Mode: Switch between manual and automatic modes by pressing the middle button"*). Seules les valeurs `0`/`1` observées à ce jour (jamais `2`/off). Décodé et écrit dans le code. |
| 3 | `KETOTEK_DP_RUNNING_STATE` | Enum | `heat=0` (chauffe active), `idle=1` | Décodé (log), jamais observé sur le réel |
| **4** | `KETOTEK_DP_HEATING_SETPOINT` | Value ×10°C | Consigne de chauffe | ✅ **Confirmé lecture ET écriture** sur le réel — le seul DP qui change réellement la consigne affichée à l'écran (voir "Cas reel"). C'est le DP unique de lecture/écriture, pas de DP séparé pour l'écho. |
| **5** | `KETOTEK_DP_LOCAL_TEMP` | Value ×10°C | Température mesurée | ✅ Confirmé sur le réel, identique à la valeur affichée à l'écran |
| **7** | `KETOTEK_DP_CHILD_LOCK` | Bool | Verrouillage enfant | ✅ Confirmé sur le réel en activant/désactivant le verrouillage physiquement |
| 28-34 | `KETOTEK_DP_SCHEDULE_WEDNESDAY`…`_TUESDAY` | Raw | Planning hebdomadaire (mercredi→mardi, mapping jour officiel) | Décodé en octets bruts uniquement, jamais observé sur le réel, pas de parsing ni d'écriture |
| 35 | `KETOTEK_DP_ERROR_OR_BATTERY_LOW` | Composite/raw | Erreur capteur ("Er" écran) / pile faible | Décodé en octets bruts uniquement, jamais observé sur le réel |
| 36 | `KETOTEK_DP_FROST_PROTECTION` | Bool | Protection hors-gel (ouvre à 5°C, ferme à 8°C) | Décodé (log), jamais observé sur le réel |
| 39 | `KETOTEK_DP_SCALE_PROTECTION` | Bool | Anti-entartrage, ouverture auto vanne toutes les 2 semaines ("Ad" écran) | Décodé (log), jamais observé sur le réel |
| 47 | `KETOTEK_DP_LOCAL_TEMP_CALIBRATION` | Value, signé | Calibration sonde | Décodé (log), jamais observé sur le réel |
| 101 | `KETOTEK_DP_PI_HEATING_DEMAND` | Raw | Modulation de vanne, probablement 0-100% | Décodé (log), jamais observé sur le réel |

DP2/4/5/7 avaient déjà été confirmés empiriquement (bouton, molette, verrouillage physiques) **avant** de trouver cette source ; elle les recoupe exactement et ajoute le reste de la table sans qu'aucun autre DP n'ait encore été testé sur le réel.

## Notes historiques

Trois tables DP se sont succédé au fil du projet, par ordre de fiabilité croissante :

1. **`old/TUYA_ZIGBEE_PROTOCOL.md`** (guess initial, avant d'avoir la vraie tête) : DP02="température" (faux — c'est en fait `system_mode`), DP04=consigne **ou** état de chauffe selon le type (partiellement juste : DP4 est bien la consigne), DP07=Child Lock (juste, comme confirmé). Un mapping deviné, à ne pas suivre aveuglément, mais qui a eu raison sur DP4/DP5/DP7 par coïncidence ou intuition correcte.
2. **Table Saswell/KETOTEK** (déduite par ressemblance de famille de devices, DP101-109/123-129/DP40 etc.) : entièrement **fausse pour ce device** — jamais un seul de ces DP n'a été observé sur le vrai KTF0177. Cette tête n'est pas un Saswell.
3. **Table AVATTO ME167_1 / TS0601_thermostat_5** (actuelle, voir "DataPoints confirmés" ci-dessus) : identifiée via le fingerprint exact (`manufacturerName`/`modelIdentifier` lus sur le cluster Basic réel) et croisée avec `zigbee-herdsman-converters` — c'est la table qui fait foi désormais.
* Bug ARRAY vs SET (`ESP_ZB_ZCL_ATTR_TYPE_ARRAY` cassant l'envoi) : voir "Cas simple" → "Bug résolu pendant la validation matérielle".

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
| `device_left` | Départ spontané d'un device du réseau Zigbee (hors révocation demandée par la RPI, ex. reset usine du device) | `short_addr` |
| `devices_list` | Réponse à la commande `list_devices` : état courant tel que vu par l'ESP32 (peut différer de la liste connue de la RPI si des devices ont rejoint/quitté depuis) | `devices` : tableau de `{ short_addr, ieee_addr, manufacturer, model }` |
| `known_devices_request` | Émis une fois au démarrage de l'ESP32, avant toute ouverture du réseau Zigbee | *(vide)* |

### Sens RPI vers ESP32

| `type` | Effet côté gateway | Contenu de `data` |
|---|---|---|
| `set_heating_setpoint` | Écrit la consigne de chauffe (DP4) sur la tête | `short_addr`, `value` (°C, ex. `21.0`) |
| `set_system_mode` | Écrit le mode (DP2) sur la tête | `short_addr`, `value` (`auto`/`heat`/`off`) |
| `set_child_lock` | Écrit le verrouillage enfant (DP7) sur la tête | `short_addr`, `value` (bool) |
| `permit_join` | Ouvre la fenêtre d'appairage Zigbee (le réseau est **fermé par défaut**, y compris au démarrage — voir « Appairage » ci-dessous) | `duration_sec` (ex. `180`) |
| `list_devices` | Retourne l'état courant des devices appairés côté ESP32 | *(vide)* |
| `remove_device` | Révoque l'appairage d'un device : quitte le réseau Zigbee (ZDO Leave) et le retire de la table en mémoire | `short_addr` |
| `known_devices_list` | Réponse à `known_devices_request` : liste des devices déjà connus, source de vérité côté RPI | `devices` : tableau de `{ short_addr, ieee_addr, manufacturer, model }` |

Chaque commande envoyée par la RPI porte un `command_id` (identifiant unique généré côté RPI, ex. compteur ou UUID court) afin de pouvoir corréler la `command_ack` correspondante.

### Notes d'implémentation
* Les champs de `status_report`/`set_*` reprennent uniquement les DataPoints **confirmés** sur le vrai KETOTEK KTF0177 dans le prototype (DP2/DP4/DP5/DP7 — voir `prototype/SPECIFICATIONS.md` § « DataPoints confirmés »). Les autres DP déjà décodés en lecture seule côté gateway (planning hebdomadaire DP28-34, protection hors-gel DP36, anti-entartrage DP39, calibration DP47, etc.) ne sont pas encore exposés dans ce protocole UART ; extension possible une fois un besoin identifié côté RPI/UI.
* Le réseau Zigbee est **fermé par défaut**, y compris au démarrage de l'ESP32 : aucun nouveau device ne peut rejoindre tant que la RPI n'a pas envoyé `permit_join`. C'est un changement volontaire par rapport au prototype, où `esp_zb_bdb_open_network(180)` était appelé automatiquement à chaque boot.
* `list_devices`, `remove_device` et `permit_join` correspondent aux commandes CLI encore non implémentées côté gateway dans le prototype (`TODO(THERMOSTAT_ENABLE_CLI)`) — il faudra les développer sur l'ESP32 avant de pouvoir les piloter depuis la RPI. `permit_join` peut s'appuyer directement sur `esp_zb_bdb_open_network()`, déjà utilisé au démarrage de la gateway dans le prototype.
* La RPI est la source de vérité de la liste des devices connus (persistée côté RPI, pas sur l'ESP32). Au démarrage, l'ESP32 envoie `known_devices_request` et attend la réponse `known_devices_list` de la RPI pour reconstruire sa table en mémoire avant d'interagir avec le réseau Zigbee — ceci remplace le besoin de persistance NVS locale des devices appairés côté ESP32.
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

## Rasberry PI Zero
* persistence des données : SQLite avec le mode WAL activé

### Dialogue avec l'ESP32-C6-ZERO

La couche qui parle le protocole UART décrit plus haut s'appuie sur trois tables SQLite : la liste des devices connus (source de vérité renvoyée à l'ESP32 via `known_devices_list`), l'historique des relevés (`status_report`), et le suivi des commandes envoyées (pour corréler les `command_ack`). Comme la RPI est la seule source d'horodatage fiable du système (pas de synchro horaire côté têtes, cf. `TUYA_MCU_SYNC_TIME` non géré), tous les `ts` ci-dessous sont l'heure de réception côté RPI, pas une horloge du device.

#### Table `devices`

```sql
CREATE TABLE devices (
    short_addr    TEXT PRIMARY KEY,   -- adresse réseau Zigbee courante, identifiant utilisé dans tout le protocole UART (ex. '0x3d6e')
    ieee_addr     TEXT NOT NULL,      -- adresse IEEE (MAC), stable même si le short_addr change après un réappairage
    manufacturer  TEXT,
    model         TEXT,
    paired_at     TEXT NOT NULL,      -- ISO8601, date du premier pairing_request reçu pour ce short_addr
    last_seen_at  TEXT,               -- ISO8601, dernier message reçu pour ce device (pairing_request ou status_report)
    revoked_at    TEXT                -- ISO8601, NULL tant que le device est appairé ; renseigné sur remove_device ou device_left
);

-- un même device physique (ieee_addr) ne doit apparaître qu'une fois comme actif,
-- même s'il a été révoqué puis réappairé sous un nouveau short_addr entretemps
CREATE UNIQUE INDEX idx_devices_ieee_active ON devices(ieee_addr) WHERE revoked_at IS NULL;
```

#### Table `readings`

```sql
CREATE TABLE readings (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    short_addr        TEXT NOT NULL REFERENCES devices(short_addr),
    ts                TEXT NOT NULL,   -- ISO8601, horodatage RPI de réception
    system_mode       TEXT,            -- 'auto' / 'heat' / 'off', NULL si absent du rapport (DP2)
    running_state     TEXT,            -- 'heat' / 'idle', NULL si absent (DP3)
    heating_setpoint  REAL,            -- °C, NULL si absent (DP4)
    local_temp        REAL,            -- °C, NULL si absent (DP5)
    child_lock        INTEGER          -- 0/1, NULL si absent (DP7)
);

CREATE INDEX idx_readings_short_addr_ts ON readings(short_addr, ts);
```

#### Table `commands`

```sql
CREATE TABLE commands (
    command_id  TEXT PRIMARY KEY,      -- généré côté RPI, repris tel quel dans le command_ack de retour
    type        TEXT NOT NULL,         -- set_heating_setpoint / set_system_mode / set_child_lock / permit_join / remove_device / list_devices
    short_addr  TEXT,                  -- device ciblé, NULL pour permit_join / list_devices
    payload     TEXT,                  -- JSON brut du champ data envoyé (hors type/command_id)
    sent_at     TEXT NOT NULL,
    acked_at    TEXT,                  -- NULL tant qu'aucun command_ack n'a été reçu
    status      TEXT NOT NULL DEFAULT 'pending',  -- pending / ok / error
    error       TEXT
);

CREATE INDEX idx_commands_status ON commands(status);
```

#### Requêtes associées à chaque message du protocole UART

**Au démarrage de l'ESP32, réponse à `known_devices_request`** — reconstituer la `known_devices_list` :
```sql
SELECT short_addr, ieee_addr, manufacturer, model
FROM devices
WHERE revoked_at IS NULL;
```

**Réception d'un `pairing_request`** — upsert du device (un réappairage sous le même `short_addr` met simplement à jour la ligne, un réappairage après révocation efface `revoked_at`) :
```sql
INSERT INTO devices (short_addr, ieee_addr, manufacturer, model, paired_at, last_seen_at, revoked_at)
VALUES (:short_addr, :ieee_addr, :manufacturer, :model, :now, :now, NULL)
ON CONFLICT(short_addr) DO UPDATE SET
    ieee_addr    = excluded.ieee_addr,
    manufacturer = excluded.manufacturer,
    model        = excluded.model,
    last_seen_at = excluded.last_seen_at,
    revoked_at   = NULL;
```

**Réception d'un `status_report`** — insertion du relevé (seuls les champs présents dans le message sont fournis, les autres restent `NULL`) puis mise à jour de la fraîcheur du device :
```sql
INSERT INTO readings (short_addr, ts, system_mode, running_state, heating_setpoint, local_temp, child_lock)
VALUES (:short_addr, :now, :system_mode, :running_state, :heating_setpoint, :local_temp, :child_lock);

UPDATE devices SET last_seen_at = :now WHERE short_addr = :short_addr;
```

**Réception d'un `device_left`** (départ spontané) — révocation locale, sans passer par une commande `remove_device` :
```sql
UPDATE devices SET revoked_at = :now WHERE short_addr = :short_addr AND revoked_at IS NULL;
```

**Envoi d'une commande vers l'ESP32** (`set_heating_setpoint`, `set_system_mode`, `set_child_lock`, `permit_join`, `remove_device`, `list_devices`) — traçabilité avant émission sur l'UART :
```sql
INSERT INTO commands (command_id, type, short_addr, payload, sent_at, status)
VALUES (:command_id, :type, :short_addr, :payload_json, :now, 'pending');
```

**Réception du `command_ack`** correspondant — clôture de la commande, et pour un `remove_device` réussi, révocation effective du device :
```sql
UPDATE commands
SET acked_at = :now, status = :status, error = :error
WHERE command_id = :command_id;

-- si :type = 'remove_device' et :status = 'ok'
UPDATE devices SET revoked_at = :now WHERE short_addr = :short_addr;
```

**Réception d'une `devices_list`** (réponse à `list_devices`) — détection d'un écart entre l'état connu de la RPI et celui vu par l'ESP32 (ex. après un redémarrage de l'ESP32 avant la fin de la synchro `known_devices_list`) :
```sql
SELECT short_addr FROM devices
WHERE revoked_at IS NULL
  AND short_addr NOT IN (:short_addrs_reçus_de_l_esp32);
```

#### Requêtes côté application (écran tactile / historique)

**Dernier relevé connu par device** (affichage temps réel sur l'écran Waveshare) :
```sql
SELECT r.*
FROM readings r
JOIN (
    SELECT short_addr, MAX(ts) AS max_ts
    FROM readings
    GROUP BY short_addr
) latest ON r.short_addr = latest.short_addr AND r.ts = latest.max_ts;
```

**Historique pour une courbe de température** (ex. dernières 24h d'un device) :
```sql
SELECT ts, local_temp, heating_setpoint
FROM readings
WHERE short_addr = :short_addr AND ts >= :depuis
ORDER BY ts;
```

**Purge / rétention** (à planifier périodiquement pour ne pas faire grossir indéfiniment la base sur la carte SD) :
```sql
DELETE FROM readings WHERE ts < :date_limite;
```

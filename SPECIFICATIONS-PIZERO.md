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

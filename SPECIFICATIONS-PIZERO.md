## Rasberry PI Zero
* persistence des données : SQLite avec le mode WAL activé
* modèle cible : Pi Zero / Zero W (premier gen, mono-cœur ARM11 1 GHz, 512 Mo RAM) — contrainte forte pour les choix de stack ci-dessous

## Stack applicative

Le matériel visé (mono-cœur, RAM limitée) écarte d'office les stacks UI lourdes (moteur OpenGL, navigateur) ; les choix ci-dessous privilégient le rendu direct et un runtime interprété léger, suffisant vu que l'appli est très majoritairement I/O-bound (UART, SQLite, tick périodique) plutôt que CPU-bound.

| Couche | Choix | Raison |
|---|---|---|
| Langage | Python 3 | Écosystème adapté à tous les besoins de l'appli (UART, JSON, SQLite), itération rapide, cohérent avec les structures déjà spécifiées (tables SQL, JSON de programmation) |
| OS | Raspberry Pi OS Lite, sans environnement de bureau | Libère toute la RAM/CPU disponibles pour l'appli plutôt que pour X11/un compositeur |
| UI tactile | [LVGL](https://lvgl.io/) (bindings Python), rendu direct sur le framebuffer Linux (`/dev/fb0`), entrée tactile via `evdev` | Pas de serveur X ni de rendu OpenGL ES à charge du GPU ; bibliothèque conçue à l'origine pour du matériel bien plus contraint qu'un Pi Zero (microcontrôleurs), donc large marge sur ce SoC ; widgets prêts à l'emploi (boutons, listes, sliders, clavier virtuel) correspondant aux écrans de [SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md) |
| Communication UART | `pyserial` | Implémentation standard du protocole décrit dans [SPECIFICATIONS-UART.md](SPECIFICATIONS-UART.md) |
| Persistance | `sqlite3` (module standard) | Cf. ligne ci-dessus (WAL) |
| API REST | **FastAPI** + **Uvicorn** (sans l'extra `[standard]`, un seul worker) | Génère automatiquement le schéma OpenAPI et sert Swagger UI (`/docs`) / ReDoc (`/redoc`) sans configuration manuelle, à partir des modèles Pydantic des ressources de [SPECIFICATION-API.md](SPECIFICATION-API.md) (Device, Program, Event, Preset, Override...) — pratique pour tester l'API manuellement en SSH ([INSTALLATION-PIZERO.md](INSTALLATION-PIZERO.md)) |
| Lancement | Service systemd | Démarrage automatique au boot, redémarrage après déploiement — voir [INSTALLATION-PIZERO.md](INSTALLATION-PIZERO.md) |

Alternatives écartées pour l'IHM, du fait du matériel visé :
* **Kivy / PyQt / PySide** — moteurs de rendu (OpenGL ES pour Kivy, Qt complet pour PyQt/PySide) trop gourmands pour un mono-cœur 512 Mo en parallèle du backend SQLite/UART ; latence perceptible rapportée sur Pi Zero premier gen dans des projets similaires.
* **UI web (ex. Flask + Chromium en mode kiosque)** — Chromium hors de portée en RAM/CPU sur ce modèle.
* **Tkinter** — modèle de widgets peu adapté au tactile, performances médiocres même sans X11 complet.

### Point de vigilance FastAPI sur Pi Zero W (armv6l)

FastAPI s'appuie sur Pydantic v2, dont le cœur (`pydantic-core`) est écrit en Rust — sur une architecture aussi ancienne que l'armv6l du Pi Zero/Zero W premier gen, ça fait craindre une compilation Rust sur la carte elle-même (irréaliste sur un mono-cœur 512 Mo). En pratique :

* PyPI seul ne publie pas de wheel `manylinux` pour armv6l (seulement armv7l et plus) — `pip` en configuration générique tenterait donc de compiler depuis les sources.
* **[piwheels](https://www.piwheels.org/project/pydantic-core/)** — l'index de wheels précompilées pour Raspberry Pi, utilisé par défaut par `pip` sur Raspberry Pi OS (`/etc/pip.conf` le pointe déjà) — publie bien des wheels **armv6l précompilées** pour `pydantic-core`, y compris sur les versions récentes (Python 3.11/3.13, alignées avec Raspberry Pi OS Bullseye/Bookworm/Trixie).

⚠️ Ne pas modifier/désactiver la configuration pip par défaut de Raspberry Pi OS (qui pointe vers piwheels), sous peine de retomber sur une compilation Rust sur la carte. Filet de sécurité si un wheel venait un jour à manquer : Flask + `flask-smorest` (100 % Python, basé sur `marshmallow`, aucune dépendance Rust).

Pour rester léger sur ce matériel : installer `uvicorn` sans l'extra `[standard]` (évite `uvloop`/`httptools`/`watchfiles`, inutiles pour un seul client local à faible cadence de requêtes), un seul worker, et servir les assets Swagger UI en local plutôt que depuis un CDN (le thermostat n'a pas d'accès Internet garanti en continu).

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

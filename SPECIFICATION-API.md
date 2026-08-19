# API REST — objets manipulés par les écrans

API HTTP/JSON exposée en local par le processus backend de la Raspberry Pi Zero (protocole UART, SQLite, résolution de planning — voir [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md) et [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md)), consommée par la couche IHM tactile ([SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md)). Séparer les deux par une API HTTP plutôt que par des appels directs en mémoire garde le backend testable indépendamment de l'IHM — utile en particulier pour vérifier un comportement via `curl` en SSH (cf. [INSTALLATION-PIZERO.md](INSTALLATION-PIZERO.md)) sans dépendre d'une capture d'écran.

## Conventions générales

* **Base URL** : `http://127.0.0.1:8080/api` — l'API n'écoute que sur la boucle locale (`127.0.0.1`), elle n'est pas exposée sur le réseau. Aucune authentification : le seul client est le processus IHM sur la même machine. Si un accès distant devient utile un jour (appli mobile, etc.), une authentification devra être ajoutée avant d'ouvrir l'écoute au-delà du loopback — hors périmètre de cette version.
* **Format** : JSON uniquement, en entrée comme en sortie (`Content-Type: application/json`).
* **Dates** : ISO 8601 (`"2026-02-06T00:00:00"`), cohérent avec [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md) et [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md).
* **Verbes** :
  * `GET` — lecture, sans effet de bord.
  * `POST` — création d'une sous-ressource, ou déclenchement d'une action non idempotente sans ressource naturelle (ex. `permit-join`, `accept`, `refuse`).
  * `PUT` — remplacement complet d'une ressource identifiée (ex. valeur d'un preset, contenu d'un jour du planning).
  * `PATCH` — mise à jour partielle (ex. renommer un device sans retoucher ses champs techniques).
  * `DELETE` — suppression/annulation.
* **Codes HTTP** : `200`/`201`/`204` en succès ; `400` (payload invalide) ; `404` (ressource inconnue) ; `409` (conflit métier, ex. suppression d'un preset encore utilisé) ; `502` (l'ESP32 n'a pas acquitté la commande UART sous-jacente, cf. `command_ack` dans [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md)).
* **Erreurs** — enveloppe uniforme :
  ```json
  { "error": { "code": "esp32_timeout", "message": "Pas de command_ack reçu de l'ESP32 dans le délai imparti" } }
  ```
* **Commandes vers l'ESP32** : les endpoints qui déclenchent une commande UART (`permit_join`, `remove_device`, cf. [SPECIFICATIONS-UART.md](SPECIFICATIONS-UART.md)) répondent **de façon synchrone**, en attendant le `command_ack` correspondant (avec un timeout, → `502` s'il n'arrive pas) — l'IHM n'a pas besoin de faire du polling. Exception : `permit-join` n'attend que l'accusé d'ouverture du réseau, pas les 180 secondes de la fenêtre elle-même.

## Écarts à combler dans le modèle de données existant

Certains endpoints ci-dessous nécessitent un petit ajout au schéma SQLite actuel, signalé ici plutôt que modifié silencieusement :

| Ajout nécessaire | Concerne | Détail |
|---|---|---|
| `devices.friendly_name` (`TEXT`, nullable) | `PATCH /api/devices/{short_addr}` | La table `devices` de [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md) n'a pas encore de nom convivial ; [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md) l'anticipait déjà (« nécessite d'y ajouter un nom convivial par device »). Tant que `friendly_name IS NULL`, l'API l'expose égal à `short_addr`. |
| `events[].id` (identifiant stable, ex. UUID court) | `PUT`/`DELETE /api/programs/{name}/events/{id}` | Le tableau `events` de [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md) n'a pas d'identifiant par élément ; en ajouter un rend chaque événement adressable individuellement sans dépendre de sa position dans le tableau. |
| Table `boiler_override` (ligne unique : `mode`, `updated_at`) | `GET`/`PUT /api/boiler` | Rien dans le schéma actuel ne trace un forçage manuel de la chaudière (écran Administration de [SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md)) ; une table dédiée, séparée de `program_config`, garde ce réglage indépendant du planning. |
| Session d'appairage (état en mémoire, non persisté) | `GET`/`POST /api/pairing/...` | La fenêtre « nouveau device détecté → Accepter/Refuser » est éphémère par nature (limitée aux 180 s de la fenêtre) ; pas besoin de table SQL, un simple état en mémoire du processus backend suffit (liste des `pairing_request` reçus depuis le dernier `permit_join`, avec leur statut `accepted`). |

## Accueil — vue d'ensemble

Un seul endpoint agrégé pour l'écran Accueil ([SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md) §Accueil), afin d'éviter plusieurs allers-retours HTTP depuis l'IHM :

| Méthode | Chemin | Description |
|---|---|---|
| `GET` | `/api/home` | Ambiance locale, dernier relevé par pièce, programme en cours et prochaine transition, état chaudière |

```json
{
  "ambient": { "temperature": 20.4, "humidity": 45.0, "pressure": 1013.2, "ts": "2026-08-19T18:32:00" },
  "rooms": [
    { "short_addr": "0x3d6e", "friendly_name": "chambre", "local_temp": 19.8, "heating_setpoint": 19.0, "last_seen_at": "2026-08-19T18:30:12" }
  ],
  "current_program": {
    "name": "semaine-normale",
    "source": "schedule",
    "next_transition": { "at": "2026-08-19T22:00:00", "rooms": "*", "preset": "nuit" }
  },
  "boiler": { "mode": "auto", "relay_state": false }
}
```

`current_program.source` vaut `"override"` si une dérogation est active (cf. [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md) §Résolution) ; dans ce cas un champ `override_id` est ajouté.

## Ambiance

| Méthode | Chemin | Description |
|---|---|---|
| `GET` | `/api/ambient` | Dernière lecture du capteur GY-BME280 local à la RPI |

```json
{ "temperature": 20.4, "humidity": 45.0, "pressure": 1013.2, "ts": "2026-08-19T18:32:00" }
```

## Devices (têtes thermostatiques)

Reflète la table `devices` de [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md), utilisé par l'écran **Administration → Devices connus** ([SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md)).

| Méthode | Chemin | Description |
|---|---|---|
| `GET` | `/api/devices` | Liste des devices actifs (`revoked_at IS NULL`), avec dernier relevé |
| `GET` | `/api/devices/{short_addr}` | Détail d'un device |
| `GET` | `/api/devices/{short_addr}/readings?since=ISO8601` | Historique des relevés (ex. courbe 24h, cf. requête « Historique pour une courbe » de [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md)) |
| `PATCH` | `/api/devices/{short_addr}` | Renommer (`{ "friendly_name": "chambre" }`) |
| `DELETE` | `/api/devices/{short_addr}` | Supprimer (déclenche `remove_device` UART, révoque en base à l'acquittement) |

```json
{
  "short_addr": "0x3d6e",
  "ieee_addr": "00:12:4b:00:1a:2b:3c:4d",
  "manufacturer": "_TZE200_p3dbf6qs",
  "model": "TS0601",
  "friendly_name": "chambre",
  "paired_at": "2026-08-08T10:00:00",
  "last_seen_at": "2026-08-19T18:30:12",
  "latest_reading": { "system_mode": "auto", "running_state": "heat", "heating_setpoint": 19.0, "local_temp": 19.8, "child_lock": false }
}
```

`GET /api/devices/{short_addr}/readings` :
```json
[
  { "ts": "2026-08-19T18:00:00", "local_temp": 19.5, "heating_setpoint": 19.0 },
  { "ts": "2026-08-19T18:30:00", "local_temp": 19.8, "heating_setpoint": 19.0 }
]
```

## Appairage

Écran **Administration → Appairage** ([SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md)). S'appuie sur les messages `permit_join`/`pairing_request`/`remove_device` du [protocole UART](SPECIFICATIONS-UART.md).

| Méthode | Chemin | Description |
|---|---|---|
| `POST` | `/api/pairing/permit-join` | Ouvre le réseau (`{ "duration_sec": 180 }`), répond dès l'acquittement de l'ouverture |
| `GET` | `/api/pairing/session` | État de la fenêtre courante et devices détectés en attente |
| `POST` | `/api/pairing/session/devices/{short_addr}/accept` | Confirme le device (déjà en base via l'upsert automatique sur `pairing_request`, cf. [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md)) — le retire simplement de la liste d'attente |
| `POST` | `/api/pairing/session/devices/{short_addr}/refuse` | Déclenche `remove_device` pour le révoquer immédiatement |

`POST /api/pairing/permit-join` → `201` :
```json
{ "opened_at": "2026-08-19T18:40:00", "expires_at": "2026-08-19T18:43:00" }
```

`GET /api/pairing/session` :
```json
{
  "active": true,
  "expires_at": "2026-08-19T18:43:00",
  "pending_devices": [
    { "short_addr": "0x4a1f", "ieee_addr": "00:12:4b:...", "manufacturer": "_TZE200_p3dbf6qs", "model": "TS0601", "detected_at": "2026-08-19T18:41:02" }
  ]
}
```

## Chaudière

Écran **Administration → Chaudière** ([SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md)). Local au relais (GPIO), aucun échange UART/Zigbee.

| Méthode | Chemin | Description |
|---|---|---|
| `GET` | `/api/boiler` | Mode courant et état effectif du relais |
| `PUT` | `/api/boiler/mode` | Change le mode (`{ "mode": "forced_on" \| "forced_off" \| "auto" }`) |

```json
{ "mode": "forced_on", "relay_state": true, "forced_since": "2026-08-19T18:00:00" }
```

`mode: "auto"` rend la main au pilotage automatique par le programme en cours (cf. `running_state` résolu, [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md)).

## Consignes (presets)

Écran **Programmation → Consignes** ([SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md)), reflète `presets` dans [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md).

| Méthode | Chemin | Description |
|---|---|---|
| `GET` | `/api/presets` | Liste `{ nom → température }` |
| `GET` | `/api/presets/{name}` | Détail d'un preset |
| `PUT` | `/api/presets/{name}` | Crée ou remplace (`{ "temperature": 19 }`) |
| `DELETE` | `/api/presets/{name}` | Supprime — `409` si encore référencé par un événement d'un programme |

## Programmes

Écran **Programmation → Bibliothèque de programmes** ([SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md)), reflète `programs` dans [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md).

| Méthode | Chemin | Description |
|---|---|---|
| `GET` | `/api/programs` | Liste des programmes (nom + nombre d'événements) |
| `GET` | `/api/programs/{name}` | Détail : `anchors` + `events` |
| `POST` | `/api/programs` | Crée un programme (`{ "name": "...", "anchors": {...} }`) |
| `PATCH` | `/api/programs/{name}` | Modifie `anchors` ou renomme |
| `DELETE` | `/api/programs/{name}` | Supprime — `409` s'il est référencé par `week_schedule` ou un `override` |
| `POST` | `/api/programs/{name}/events` | Ajoute un événement, l'API lui attribue un `id` |
| `PUT` | `/api/programs/{name}/events/{event_id}` | Remplace un événement |
| `DELETE` | `/api/programs/{name}/events/{event_id}` | Supprime un événement |

`GET /api/programs/{name}` :
```json
{
  "name": "semaine-normale",
  "anchors": { "wake_time": "07:00" },
  "events": [
    { "id": "ev1", "time": "00:00", "rooms": "*", "preset": "nuit" },
    { "id": "ev2", "time": "00:00", "rooms": ["chambre"], "preset": "chambre-nuit" },
    { "id": "ev3", "time": { "anchor": "wake_time", "offset_min": -30 }, "rooms": ["chambre"], "preset": "chambre-reveil" }
  ]
}
```

## Planning hebdomadaire

Écran **Programmation → Semaine** ([SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md)), reflète `week_schedule` dans [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md).

| Méthode | Chemin | Description |
|---|---|---|
| `GET` | `/api/week-schedule` | Les 7 jours, programme assigné + surcharges d'ancres |
| `PUT` | `/api/week-schedule/{day}` | Remplace l'assignation d'un jour (`day` ∈ `mon`..`sun`) |

```json
{ "program": "semaine-normale", "anchor_overrides": { "wake_time": "07:00" } }
```

## Dérogations

Écran **Programmation → Dérogations** et raccourci de l'écran Accueil ([SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md)), reflète `overrides` dans [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md).

| Méthode | Chemin | Description |
|---|---|---|
| `GET` | `/api/overrides` | Liste, avec statut calculé (`upcoming` / `active` / `past`) |
| `POST` | `/api/overrides` | Crée (`{ "name", "program", "start", "end" }`) — `start` omis = maintenant |
| `GET` | `/api/overrides/{id}` | Détail |
| `DELETE` | `/api/overrides/{id}` | Annule (retour immédiat au planning hebdomadaire si elle était active) |

## Résolution du planning (vérification)

Endpoint de lecture seule, sans écran dédié mais utile pour vérifier l'algorithme de résolution ([SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md) §Résolution) sans attendre le tick du job planifié — pratique pour tester via `curl` en SSH ([INSTALLATION-PIZERO.md](INSTALLATION-PIZERO.md)) plutôt que par capture d'écran.

| Méthode | Chemin | Description |
|---|---|---|
| `GET` | `/api/schedule/resolve?at=ISO8601` | Consigne effective (preset + température) par pièce à l'instant donné (défaut : maintenant) |

```json
{
  "at": "2026-08-19T18:45:00",
  "source": { "type": "schedule", "day": "wed", "program": "semaine-normale" },
  "rooms": { "chambre": { "preset": "chambre-nuit", "temperature": 17 }, "salon": { "preset": "nuit", "temperature": 15 } }
}
```

## Table de correspondance écran → endpoints

| Écran ([SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md)) | Endpoints |
|---|---|
| Accueil | `GET /api/home`, `POST /api/overrides` |
| Programmation → Semaine | `GET`/`PUT /api/week-schedule` |
| Programmation → Bibliothèque de programmes | `GET`/`POST`/`PATCH`/`DELETE /api/programs`, `POST`/`PUT`/`DELETE /api/programs/{name}/events` |
| Programmation → Consignes | `GET`/`PUT`/`DELETE /api/presets` |
| Programmation → Dérogations | `GET`/`POST`/`DELETE /api/overrides` |
| Administration → Appairage | `POST /api/pairing/permit-join`, `GET /api/pairing/session`, `POST .../accept`, `POST .../refuse` |
| Administration → Devices connus | `GET /api/devices`, `PATCH`/`DELETE /api/devices/{short_addr}` |
| Administration → Chaudière | `GET`/`PUT /api/boiler` |

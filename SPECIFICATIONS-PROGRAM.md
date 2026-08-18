# Programmation du chauffage

Structure de données JSON pour représenter le programme de chauffage décrit dans [application.md](application.md) : consignes nommées réutilisables, programmes journaliers construits comme une suite d'événements horodatés, planning hebdomadaire (un programme par jour, avec une heure de réveil ajustable), et dérogations temporaires (type "congés") qui remplacent le planning par défaut sur une période.

## Principes de modélisation

* Une **pièce** (`room`) correspond à une tête thermostatique (voir la table `devices` de [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md)) — nécessite d'y ajouter un nom convivial par device (fonction "Renomer" de l'écran Administration dans `application.md`), utilisé ici comme identifiant de pièce.
* Un **preset** est une consigne nommée (`nuit`, `presence`, `absence`, `teletravaille`, `congés`...) associée à une température en °C — un même nom de preset a la même valeur partout où il est utilisé.
* Un **programme** (`program`) est une suite d'**événements** : à une heure donnée, une ou plusieurs pièces (ou toutes) passent sur un preset. Les pièces non concernées par un événement **ne changent pas** — le modèle est événementiel (on ne stocke que les transitions), pas une grille horaire complète par pièce.
* Le **planning hebdomadaire** assigne un programme à chaque jour de la semaine, avec la possibilité de personnaliser l'heure de réveil du jour (elle "peut changer en fonction du jour de la semaine" — cf. `application.md`).
* Une **dérogation** (`override`) remplace temporairement le planning hebdomadaire par un programme donné, sur une période `[start, end)` — c'est le mécanisme des "programmes types que l'on peut activer sur une période" (ex. congés).

## Presets

Dictionnaire nom → température (°C). Défini une fois, réutilisé par tous les programmes.

```json
{
  "presets": {
    "nuit": 15,
    "chambre-nuit": 17,
    "chambre-reveil": 19,
    "chambre-couche": 19,
    "reveille": 21,
    "presence": 19,
    "teletravaille": 21,
    "absence": 17,
    "soiree": 21,
    "conges": 15
  }
}
```

## Programmes (bibliothèque d'événements)

| Champ | Type | Description |
|---|---|---|
| `anchors` | objet `{ nom: "HH:MM" }` | Repères horaires nommés utilisables par les événements du programme (ex. `wake_time`). Leur valeur par défaut est définie ici, mais peut être surchargée par jour dans le planning hebdomadaire. |
| `events` | tableau | Liste d'événements, triée par heure croissante dans le JSON (l'ordre sert aussi de tie-break, voir "Résolution" plus bas). |
| `events[].time` | `"HH:MM"` **ou** `{ "anchor": "nom", "offset_min": ±N }` | Heure absolue, ou heure relative à un anchor du programme (ex. 30 min avant le réveil : `{"anchor": "wake_time", "offset_min": -30}`). |
| `events[].rooms` | `"*"` ou tableau de noms de pièces | `"*"` = toutes les pièces. Un événement `"*"` suivi d'un événement plus spécifique à la **même heure** est écrasé pour les pièces listées dans ce second événement (permet d'exprimer "toutes les pièces sauf X", voir exemple `teletravail` ci-dessous). |
| `events[].preset` | string | Nom d'un preset défini dans `presets`. |

```json
{
  "programs": {
    "semaine-normale": {
      "anchors": { "wake_time": "07:00" },
      "events": [
        { "time": "00:00", "rooms": "*", "preset": "nuit" },
        { "time": "00:00", "rooms": ["chambre"], "preset": "chambre-nuit" },
        { "time": { "anchor": "wake_time", "offset_min": -30 }, "rooms": ["chambre"], "preset": "chambre-reveil" },
        { "time": { "anchor": "wake_time", "offset_min": -30 }, "rooms": ["salon"], "preset": "reveille" },
        { "time": { "anchor": "wake_time", "offset_min": -30 }, "rooms": ["cuisine"], "preset": "presence" }
      ]
    },
    "teletravail": {
      "anchors": {},
      "events": [
        { "time": "09:00", "rooms": "*", "preset": "absence" },
        { "time": "09:00", "rooms": ["bureau"], "preset": "teletravaille" },
        { "time": "11:00", "rooms": ["salon", "cuisine"], "preset": "presence" },
        { "time": "13:00", "rooms": ["salon", "cuisine"], "preset": "absence" },
        { "time": "16:00", "rooms": "*", "preset": "absence" },
        { "time": "19:30", "rooms": ["salon"], "preset": "soiree" },
        { "time": "19:30", "rooms": ["cuisine"], "preset": "presence" },
        { "time": "21:00", "rooms": ["chambre"], "preset": "chambre-couche" },
        { "time": "22:00", "rooms": "*", "preset": "nuit" },
        { "time": "22:00", "rooms": ["chambre"], "preset": "chambre-nuit" }
      ]
    },
    "absent": {
      "anchors": {},
      "events": [
        { "time": "00:00", "rooms": "*", "preset": "absence" }
      ]
    },
    "conges": {
      "anchors": {},
      "events": [
        { "time": "00:00", "rooms": "*", "preset": "conges" }
      ]
    }
  }
}
```

`teletravail` illustre la règle "toutes les pièces sauf X" : à `09:00`, l'événement `"*" → absence` est appliqué puis immédiatement écrasé pour `bureau` par l'événement suivant à la même heure — pas besoin d'opérateur "sauf" dédié dans le schéma.

## Planning hebdomadaire

Associe un programme à chaque jour, avec une éventuelle surcharge des `anchors` de ce programme pour ce jour précis (c'est le mécanisme qui permet à l'heure de réveil de varier par jour sans dupliquer le programme).

```json
{
  "week_schedule": {
    "mon": { "program": "semaine-normale", "anchor_overrides": { "wake_time": "07:00" } },
    "tue": { "program": "teletravail" },
    "wed": { "program": "semaine-normale", "anchor_overrides": { "wake_time": "07:00" } },
    "thu": { "program": "teletravail" },
    "fri": { "program": "semaine-normale", "anchor_overrides": { "wake_time": "06:30" } },
    "sat": { "program": "semaine-normale", "anchor_overrides": { "wake_time": "08:30" } },
    "sun": { "program": "semaine-normale", "anchor_overrides": { "wake_time": "08:30" } }
  }
}
```

Le mardi peut aussi basculer sur `absent` au lieu de `teletravail` en changeant simplement `week_schedule.tue.program` — c'est l'"alternative" mentionnée dans `application.md`, pas un concept séparé.

## Dérogations (`overrides`)

| Champ | Type | Description |
|---|---|---|
| `id` | string | Identifiant unique |
| `name` | string | Libellé affiché à l'écran (ex. "Congés février") |
| `program` | string | Référence à un programme de la bibliothèque, appliqué à la place du planning hebdomadaire pendant `[start, end)` |
| `start` | ISO 8601 | Début de validité (généralement "maintenant" au moment de l'activation) |
| `end` | ISO 8601 | Fin de validité |

```json
{
  "overrides": [
    {
      "id": "conges-fevrier-2026",
      "name": "Congés février",
      "program": "conges",
      "start": "2026-02-06T00:00:00",
      "end": "2026-02-13T16:00:00"
    }
  ]
}
```

Une seule dérogation est active à la fois en pratique (usage prévu : vacances/absence prolongée) ; si plusieurs se chevauchent, la plus récemment créée (`start` le plus tardif) l'emporte.

## Résolution : consigne effective d'une pièce à l'instant *t*

1. Chercher une dérogation active (`start <= t < end`) dans `overrides`. S'il y en a une, le programme à utiliser est `overrides[].program`. Sinon, c'est celui du jour courant dans `week_schedule`.
2. Résoudre les `anchors` du programme choisi : valeur par défaut du programme, éventuellement surchargée par `week_schedule[jour].anchor_overrides`.
3. Convertir chaque `events[].time` en heure absolue du jour (calcul direct si `"HH:MM"`, ou `anchor ± offset_min` sinon).
4. Pour chaque pièce, la consigne effective est celle du **dernier événement applicable** (heure ≤ t, en remontant si besoin à la veille pour couvrir la nuit — ex. l'événement `22:00 → nuit` de la veille reste actif jusqu'au premier événement du programme du jour) — en ne retenant, à heure égale, que le dernier événement du tableau qui cible cette pièce (directement ou via `"*"`).
5. Traduire le nom de preset obtenu en température via `presets`, puis comparer à la dernière valeur connue de la pièce (dernier `readings.heating_setpoint`, voir [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md)) avant d'émettre un `set_heating_setpoint` — pas d'envoi UART si la valeur n'a pas changé.

## Persistance côté RPI

Un seul blob JSON (config éditée rarement depuis l'écran tactile, pas de besoin de forte fréquence d'écriture) :

```sql
CREATE TABLE program_config (
    id         INTEGER PRIMARY KEY CHECK (id = 1),  -- ligne unique
    config     TEXT NOT NULL,   -- JSON complet : presets + programs + week_schedule + overrides
    updated_at TEXT NOT NULL
);
```

Un job planifié (ex. toutes les minutes) relit `config`, applique l'algorithme de résolution ci-dessus pour chaque pièce connue dans `devices`, et envoie les `set_heating_setpoint` nécessaires.

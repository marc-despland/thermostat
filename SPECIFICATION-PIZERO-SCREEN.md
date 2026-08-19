# Écrans de l'application (Raspberry Pi Zero)

Description des écrans affichés sur l'écran tactile Waveshare 3,5″ branché sur la RPI (voir [SPECIFICATIONS.md](SPECIFICATIONS.md)) et de la navigation entre eux. Cette application est la seule interface humaine du thermostat : elle consulte et modifie les données décrites dans [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md) (`devices`, `readings`, `commands`) et [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md) (`presets`, `programs`, `week_schedule`, `overrides`), et déclenche les commandes du [protocole UART](SPECIFICATIONS-UART.md) vers l'ESP32-C6 (`permit_join`, `remove_device`, `set_heating_setpoint`...).

## Contraintes d'IHM

L'écran 3,5″ impose un format réduit (faible résolution, usage tactile au doigt, pas de clavier physique) :

* Un seul niveau d'information dense par écran, le détail passe par un écran suivant plutôt que par un scroll long.
* Boutons et lignes de liste larges (cible tactile), listes défilantes verticalement pour les devices/programmes/événements.
* Saisie de texte (renommage, nom de programme) via clavier virtuel plein écran.
* Saisie d'heure/température via sélecteurs (roue ou +/-) plutôt que clavier, plus fiable au doigt sur petit écran.

## Navigation générale

Une barre de navigation persistante en pied d'écran donne accès aux trois sections principales : **Accueil**, **Programmation**, **Administration**. Chaque écran de second niveau ou plus (détail, édition) affiche une flèche retour en haut à gauche vers l'écran parent ; la barre du bas reste accessible pour sauter directement à une autre section. L'icône **Administration** porte un badge dès qu'un nouveau device est en attente d'acceptation (voir plus bas), visible même en dehors de cette section.

```
Accueil
├── Dérogation rapide (modale)
│
Programmation
├── Semaine (écran par défaut de la section)
│   └── Édition d'un jour                     (programme du jour + heure de réveil)
├── Bibliothèque de programmes
│   ├── Détail d'un programme                 (liste d'événements)
│   │   ├── Édition d'un événement             (heure/ancre, pièces, preset)
│   │   └── Nouvel événement
│   └── Nouveau programme
├── Consignes (presets)
│   └── Édition d'un preset                    (nom, température)
└── Dérogations
    ├── Détail / annulation d'une dérogation
    └── Nouvelle dérogation
│
Administration
├── Appairage
│   └── Nouveau device détecté → Accepter / Refuser
├── Devices connus
│   └── Détail d'un device → Renommer / Supprimer
└── Chaudière                                  (forçage ON/OFF)
```

## Accueil

Écran par défaut au démarrage / après veille de l'écran tactile. Vue d'ensemble en lecture, une seule action de modification directement accessible.

| Zone | Contenu | Source |
|---|---|---|
| Bandeau haut | Date et heure courantes ; température/humidité/pression ambiante | Capteur GY-BME280 lu localement sur la RPI |
| Liste des pièces | Une ligne par tête connue : nom convivial, dernière température mesurée, consigne actuelle, fraîcheur du relevé (`last_seen_at`) | Dernier relevé par device — requête « Dernier relevé connu par device » de [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md) |
| Programme en cours | Nom du programme actif (planning du jour ou dérogation en cours), prochaine transition prévue (heure + preset) | Résolution décrite en §« Résolution » de [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md) |
| Action | Bouton **Dérogation** ouvrant une modale : choix d'un programme de la bibliothèque + date/heure de fin → crée une entrée `overrides` avec `start = maintenant` | Écrit dans `program_config` (voir [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md) §Dérogations) |

La modale de dérogation rapide est un raccourci vers la même fonctionnalité que **Programmation → Dérogations → Nouvelle dérogation** ; les deux écrivent la même structure et se retrouvent dans la même liste.

## Programmation

Section de consultation et d'édition du planning de chauffe.

### Semaine

Écran d'entrée de la section : grille des 7 jours, chacun affichant le nom du programme assigné (`week_schedule[jour].program`) et l'heure de réveil éventuellement surchargée (`anchor_overrides.wake_time`). Toucher un jour ouvre son édition : changer le programme assigné (choix dans la bibliothèque) et ajuster l'heure de réveil du jour.

### Bibliothèque de programmes

Liste des programmes existants (`semaine-normale`, `teletravail`, `absent`, `congés`...). Le détail d'un programme affiche ses événements triés par heure (heure absolue ou ancre + décalage, pièces concernées, preset appliqué) ; toucher un événement l'édite, un bouton permet d'en ajouter un. **Nouveau programme** crée une entrée vide (nom, ancres) avant d'y ajouter des événements.

### Consignes (presets)

Liste des presets (`nuit`, `presence`, `absence`...) avec leur température. Éditer un preset change sa valeur partout où il est utilisé (un même nom = une même température, cf. [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md) §Principes de modélisation).

### Dérogations

Liste des dérogations (actives, à venir), avec nom, programme associé et période `[start, end)`. Une dérogation peut être annulée avant son terme (suppression de l'entrée, retour immédiat au planning hebdomadaire). **Nouvelle dérogation** propose le même formulaire que le raccourci de l'écran Accueil.

## Administration

Section technique : gestion des devices Zigbee et pilotage manuel de la chaudière.

### Appairage

Bouton **Ouvrir le réseau (180 s)** envoyant la commande UART `permit_join` (voir [SPECIFICATIONS-UART.md](SPECIFICATIONS-UART.md)) ; un compte à rebours visuel indique le temps restant. Pendant la fenêtre, chaque `pairing_request` reçu (le device a déjà rejoint le réseau Zigbee et est upserté en base, cf. [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md) §Table `devices`) apparaît dans une liste d'attente avec son identifiant (`short_addr`, `manufacturer`/`model`) et deux actions :

* **Accepter** : le device reste dans la liste des devices connus, il devient ensuite disponible pour être renommé et affecté à une pièce dans les écrans de Programmation.
* **Refuser** : envoie `remove_device` pour le faire quitter le réseau et le révoquer immédiatement (`revoked_at`).

### Devices connus

Liste des devices non révoqués (`revoked_at IS NULL`), avec nom convivial, dernier relevé et fraîcheur (`last_seen_at`). Toucher un device ouvre son détail avec deux actions :

* **Renommer** : édite le nom convivial (le nom de pièce utilisé comme identifiant dans les programmes, cf. [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md) §Principes de modélisation).
* **Supprimer** : envoie `remove_device`, révoque le device en base à réception du `command_ack`.

### Chaudière

Bascule manuelle **ON / OFF** du relais de chauffe (Panasonic AQY212GH), en dehors de toute logique de programme — utile pour un test ou un besoin ponctuel. Tant que le forçage est actif, l'écran l'indique clairement (bandeau d'alerte) sur cet écran et sur l'Accueil, avec un bouton pour repasser en mode automatique (reprise du pilotage par le programme en cours).

## Table de navigation

| Écran source | Action | Écran destination |
|---|---|---|
| Accueil | Bouton Dérogation | Modale Dérogation rapide |
| Accueil / n'importe quel écran | Barre du bas | Accueil / Programmation / Administration |
| Programmation → Semaine | Toucher un jour | Édition d'un jour |
| Programmation → Bibliothèque | Toucher un programme | Détail du programme |
| Détail d'un programme | Toucher un événement / + | Édition d'un événement |
| Programmation → Consignes | Toucher un preset | Édition du preset |
| Programmation → Dérogations | Bouton Nouvelle dérogation | Formulaire de dérogation |
| Administration → Appairage | Ouvrir le réseau | Liste d'attente des nouveaux devices |
| Liste d'attente | Accepter / Refuser | Retour à la liste d'attente (mise à jour) |
| Administration → Devices connus | Toucher un device | Détail du device |
| Détail d'un device | Renommer / Supprimer | Retour à la liste des devices connus |

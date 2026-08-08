# 🔄 Flux d'Appairage KETOTEK - Diagramme Technique

## Flux Principal d'Appairage

```
START
  │
  ▼
┌─────────────────────────────────────┐
│  Gateway Démarrée                   │
│  - Initialise stack Zigbee          │
│  - Forme le réseau                  │
│  - Endpoint 1 créé                  │
│  - LED: 🔴                           │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  Utilisateur: permit_join 180       │
│  → esp_zb_bdb_open_network(180)     │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  Signal: NWK_SIGNAL_PERMIT_JOIN     │
│  Durée: 180 secondes                │
│  Message: "Network is open"         │
│  🟢 Réceptif aux appareils         │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  KETOTEK: Bouton Appairage          │
│  Durée: Presser 3-5 sec             │
│  Action: Factory reset              │
│  État: 🔴 LED clignotante           │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  KETOTEK: Scan Réseau               │
│  - Détecte gateway Zigbee           │
│  - Envoie: ZDO Start Request        │
│  - Demande: Rejoindre réseau        │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  Gateway: Reçoit demande            │
│  - Authentifie KETOTEK              │
│  - Assigne adresse courte           │
│  - Exemple: 0x1234                  │
│  - Configure endpoint 1             │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  Gateway: Envoie Configuration      │
│  - Transmission paramètres réseau   │
│  - Clusters Zigbee                  │
│  - Clusters Thermostat              │
│  - Sécurité Zigbee                  │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  KETOTEK: Reçoit Configuration      │
│  - Stocke adresse courte            │
│  - Stocke PAN ID réseau             │
│  - Sauvegarde configuration         │
│  - LED: 🟢 Fixe (connecté)          │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  KETOTEK: Envoie Announcement       │
│  Signal: ZDO Device Announcement    │
│  Message: "Je suis 0x1234"          │
│  Endpoint: 1                        │
│  Clusters: Thermostat (0x0201)      │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  Gateway: Reçoit Announcement       │
│  Signal: ESP_ZB_ZDO_SIGNAL_DEVICE_  │
│            ANNCE                    │
│  Traitement: zb_app_signal_handler()│
│  Cas: ZDO_SIGNAL_DEVICE_ANNCE       │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  Gateway: Enregistre Appareil       │
│  - Ajoute dans g_paired_devices[]   │
│  - Index: 0 (premier appareil)      │
│  - Struct:                          │
│    {                                │
│      short_addr: 0x1234,            │
│      endpoint: 1,                   │
│      model: "KETOTEK"               │
│    }                                │
│  - Compteur: g_paired_devices_count │
│  - Log: "Device registered"         │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  Utilisateur: list_devices          │
│  → Affiche:                         │
│    Device 1: Short Address=0x1234   │
│    Total: 1 device(s)              │
└──────────┬──────────────────────────┘
           │
           ▼
┌─────────────────────────────────────┐
│  ✅ APPAIRAGE RÉUSSI!               │
│                                     │
│  État:                              │
│  - Gateway: 🟢 Connectée            │
│  - KETOTEK: 🟢 Appairé              │
│  - Communication: Possible          │
│                                     │
│  Prêt pour:                         │
│  - Lecture température              │
│  - Modification point de consigne   │
│  - Rapports automatiques            │
└──────────┬──────────────────────────┘
           │
          END
```

---

## État Machine Zigbee

```
                    ┌─────────────────┐
                    │  FACTORY_NEW    │
                    │   (Reset)       │
                    └────────┬────────┘
                             │
                             │ esp_zb_bdb_start_top_level_commissioning()
                             │ BDB_MODE_INITIALIZATION
                             ▼
                    ┌─────────────────┐
                    │ STARTUP_PHASE   │
                    │ Initialisation  │
                    └────────┬────────┘
                             │
                             │ ZDO_SIGNAL_SKIP_STARTUP
                             ▼
                    ┌─────────────────┐
                    │ FORMATION       │
                    │ Crée réseau     │
                    └────────┬────────┘
                             │
                             │ BDB_MODE_NETWORK_FORMATION
                             │ (Si factory new)
                             ▼
                    ┌─────────────────┐
                    │ NETWORK_FORMED  │
                    │ Coordonnateur OK│
                    └────────┬────────┘
                             │
                             │ esp_zb_bdb_open_network(180)
                             │ permit_join_cmd()
                             ▼
                    ┌─────────────────┐
                    │ PERMIT_JOIN     │
        ┌───────────│  ACTIVE         │◄──────────┐
        │           │ (180 sec)       │           │
        │           └─────────────────┘           │
        │                                         │
        │                                    Timeout
        │                                         │
        │ KETOTEK detected                        │
        │                                         │
        ▼                                         │
┌──────────────────────────────────────────────┐ │
│         ZDO STEERING PHASE                    │ │
│  - KETOTEK envoie request                    │ │
│  - Gateway accepte                           │ │
│  - Assigne adresse 0x1234                    │ │
│  - Envoie configuration                      │ │
└────┬─────────────────────────────────────────┘ │
     │                                           │
     │ esp_zb_bdb_start_top_level_commissioning()│
     │ BDB_MODE_NETWORK_STEERING                │
     ▼                                           │
┌─────────────────┐                             │
│  KETOTEK JOIN   │                             │
│  - Reçoit config│                             │
│  - Stocke addr  │                             │
│  - Connecté     │                             │
└────┬────────────┘                             │
     │                                           │
     │ ZDO Device Announcement                  │
     │                                           │
     ▼                                           │
┌─────────────────────────────────────────────┐ │
│  ANNOUNCEMENT RECEIVED                       │ │
│  Signal: ZDO_SIGNAL_DEVICE_ANNCE            │ │
│                                               │ │
│  Gateway:                                    │ │
│  - Reçoit annonce                           │ │
│  - Extrait short_addr (0x1234)              │ │
│  - Ajoute dans g_paired_devices[]           │ │
│  - Incrémente compteur                      │ │
│  - Log: "Device commissioned"               │ │
└────┬─────────────────────────────────────────┘ │
     │                                           │
     │ Timeout du permit_join (180s)            │
     │                                           ├─────┘
     ▼                                           │
┌──────────────────────────┐                    │
│  NETWORK_CLOSED          │                    │
│  permit_join terminé     │                    │
│  Aucun nouvel appareil   │                    │
└──────────────────────────┘                    │
                                                │
                                (permit_join 0)─┘
```

---

## Messages Zigbee Échangés

### 1. Demande d'Appairage (KETOTEK → Gateway)

```
ZDO_REQUEST
├─ Type: Rejoin Request (ou Association Request)
├─ Source: KETOTEK (adresse IEEE)
├─ Destination: Gateway (broadcast 0xFFFF)
├─ Capability: End Device
├─ Request Type: MAC Association + Rejoinder
└─ Security: 1 (actif)
```

### 2. Réponse Gateway

```
ZDO_RESPONSE
├─ Type: Association Response
├─ Status: Success (0x00)
├─ Assigned Short Address: 0x1234
├─ Security: Join key établis
└─ PAN ID: 0x1234 (example)
```

### 3. Envoi Configuration Réseau

```
NWK_BROADCAST
├─ Destination: 0x1234 (KETOTEK)
├─ Clusters à supporter:
│  ├─ Basic (0x0000)
│  ├─ Power Configuration (0x0001)
│  ├─ Identify (0x0003)
│  ├─ Thermostat (0x0201)
│  └─ Autres requis par KETOTEK
├─ Sécurité: Clé réseau distribuée
└─ ACK: Demandé
```

### 4. Device Announcement

```
DEVICE_ANNOUNCEMENT
├─ Source: 0x1234 (KETOTEK)
├─ Destination: Broadcast (Gateway + autres)
├─ IEEE Address: 00:11:22:33:44:55:66:77
├─ NWK Address: 0x1234
├─ Capability:
│  ├─ Type: End Device
│  ├─ Power: Battery-powered
│  ├─ Receiver: Intermittent
│  └─ Security: Capable
└─ Clusters Supportés: [0x0000, 0x0201, ...]
```

---

## Timeline Chronométré

```
T = 0s
└─ Utilisateur tape: permit_join 180

T = 0.1s
└─ Gateway: esp_zb_bdb_open_network() appelé
└─ Signal: NWK_SIGNAL_PERMIT_JOIN_STATUS envoyé

T = 1-10s
└─ KETOTEK: Bouton pressé (appairage)
└─ KETOTEK: Scan réseau Zigbee lancé

T = 2-15s
└─ KETOTEK: Découvre gateway sur canal 25
└─ KETOTEK: Envoie ZDO request

T = 2.1-15.1s
└─ Gateway: Reçoit ZDO request
└─ Gateway: Authentifie KETOTEK
└─ Gateway: Assigne adresse 0x1234

T = 2.2-15.2s
└─ Gateway: Envoie configuration réseau

T = 3-16s
└─ KETOTEK: Reçoit config
└─ KETOTEK: Stocke paramètres
└─ KETOTEK: LED devient fixe (🟢)

T = 3.5-16.5s
└─ KETOTEK: Envoie Device Announcement

T = 3.6-16.6s
└─ Gateway: Reçoit Device Announcement
└─ Gateway: Appel zb_app_signal_handler()
└─ Gateway: Cas ZDO_SIGNAL_DEVICE_ANNCE
└─ Gateway: Log "New device commissioned"

T = 4-17s
└─ Gateway: Enregistre dans g_paired_devices[0]
└─ Gateway: Incrémente g_paired_devices_count à 1

T = 4.1-17.1s
└─ Utilisateur: list_devices
└─ Gateway: Affiche "Device 1: 0x1234"

T = 180s
└─ Timeout permit_join
└─ Gateway: Log "Network closed"
└─ Aucun nouvel appareil ne peut s'appairer
```

---

## Diagramme d'État de Communication

```
┌───────────────────────────────────────────────┐
│         KETOTEK State Machine                  │
└───────────────────────────────────────────────┘

         ┌─────────────────┐
         │   POWER_OFF     │
         │   (Arrêt)       │
         └────────┬────────┘
                  │ Batterie installée
                  ▼
         ┌─────────────────┐
         │   INITIALIZING  │
         │   (Démarrage)   │
         └────────┬────────┘
                  │
         ┌────────▼────────────────────────┐
         │ Déjà appairé?                   │
         └────────┬─────────────┬──────────┘
                  │ OUI         │ NON
                  │             │
            ┌─────▼────┐   ┌────▼─────────┐
            │ SCAN NWK  │   │ JOIN_PENDING │
            │ Cherche   │   │ (Attend)     │
            │ gateway   │   └────┬────────┘
            └─────┬─────┘        │
                  │           Bouton pressé (Reset)
                  │              │
            ┌─────▼──────────────▼──────┐
            │   SENDING JOIN REQUEST     │
            │   (Demande d'appairage)    │
            └─────┬────────────────────┘
                  │ Réponse reçue
                  │
            ┌─────▼──────────────────────┐
            │   RECEIVING_CONFIGURATION  │
            │   (Reçoit paramètres)      │
            └─────┬────────────────────┘
                  │
            ┌─────▼──────────────────────┐
            │   JOINED                   │
            │   (Appareil enregistré)    │
            │   LED: 🟢 Fixe             │
            └─────┬────────────────────┘
                  │ Envoie announcement
                  │
            ┌─────▼──────────────────────┐
            │   OPERATIONAL              │
            │   (Communication active)   │
            │ Peut envoyer/recevoir      │
            └────────────────────────────┘
```

---

## Structure de Données Appairage

### Avant Appairage

```c
g_paired_devices[10] = {
    { short_addr: UNINITIALIZED, endpoint: 0, model: "" },
    { short_addr: UNINITIALIZED, endpoint: 0, model: "" },
    ...
}
g_paired_devices_count = 0
```

### Après Appairage KETOTEK

```c
g_paired_devices[10] = {
    { short_addr: 0x1234, endpoint: 1, model: "KETOTEK" },
    { short_addr: UNINITIALIZED, endpoint: 0, model: "" },
    ...
}
g_paired_devices_count = 1
```

### Après Appairage Seconde Appareil

```c
g_paired_devices[10] = {
    { short_addr: 0x1234, endpoint: 1, model: "KETOTEK" },
    { short_addr: 0x5678, endpoint: 1, model: "AUTRE" },
    { short_addr: UNINITIALIZED, endpoint: 0, model: "" },
    ...
}
g_paired_devices_count = 2
```

---

## Interactions Zigbee: Lecture de Température

Après appairage réussi, voici le flux de lecture:

```
┌────────────────────────────────────────┐
│    KETOTEK mesure température          │
│    Local Temperature = 22.5°C           │
│    (Internal: 2250 × 0.01°C)           │
└──────────┬─────────────────────────────┘
           │
           ▼
┌────────────────────────────────────────┐
│  KETOTEK: Vérifie Reporting Config     │
│  Min Interval: 30s (si configuré)      │
│  Max Interval: 300s (si configuré)     │
│  Reportable Change: 50 (0.5°C)         │
│  Changement > 0.5°C depuis last report?│
└──────────┬─────────────────────────────┘
           │ Oui, ou timeout atteint
           ▼
┌────────────────────────────────────────┐
│  KETOTEK: Envoie Attribute Report       │
│  Destination: Gateway (0x0000)         │
│  Cluster: Thermostat (0x0201)          │
│  Attribute: LocalTemp (0x0000)         │
│  Value: 0x08CA (2250 en hex)           │
│  Type: INT16                           │
└──────────┬─────────────────────────────┘
           │
           ▼
┌────────────────────────────────────────┐
│  Gateway: Reçoit Report                │
│  Handler: zb_attribute_handler()       │
│  Extrait valeur: 2250                  │
│  Convertit: 2250 / 100 = 22.50°C      │
│  Log: "Temperature: 22.50°C"           │
└────────────────────────────────────────┘
```

---

## Procédure de Sécurité Zigbee

### 1. Association (Appairage)

```
KETOTEK (non-securisé)
         │
         │ Envoie ZDO Association Request (non-sécurisé)
         │
         ▼
Gateway (Coordinateur)
         │
         │ Authentifie le device
         │ (Si install code: vérifie code)
         │
         ▼
         Génère: Network Key (clé réseau)
         Génère: Link Key (clé de liaison)
         
KETOTEK reçoit keys
         │
         ▼
         Stocke clés dans NV-RAM
         
Communication sécurisée établie
(Tous les messages Zigbee après utilisent l'encryption)
```

### 2. Rôles de Sécurité

```
Gateway (Coordinateur):
├─ Trust Center
├─ Gère les clés de réseau
├─ Authentifie les appareils
└─ Distribue clés

KETOTEK (End Device):
├─ Reçoit et stocke clés
├─ Utilise clés pour chiffrer messages
├─ Peut envoyer messages authentifiés
└─ Ne peut pas ajouter d'appareils
```

---

Voilà! Un diagramme complet du flux technique d'appairage. 🎉

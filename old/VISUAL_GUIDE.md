# 🎯 Appairage Visuel Étape par Étape

## Étape 1: Gateway Prête ✓

```
┌─────────────────────────────────────┐
│         GATEWAY ESP32-C6             │
│                                      │
│  🟢 Alimentée (LED rouge/verte)     │
│  🟢 Logs affichés en série          │
│  🟢 Réseau Zigbee formé             │
│                                      │
│  "Formed network successfully"       │
└─────────────────────────────────────┘
              △
              │
          ✓ Étape 1 réussie
```

**Dans le moniteur série:**
```
I (xxx) ESP_ZB_GATEWAY: Formed network successfully 
I (xxx) ESP_ZB_GATEWAY: Network(0x1234) is open for 180 seconds
```

---

## Étape 2: Ouvrir le Réseau

```
                Console
                  △
                  │
           ┌──────┴──────┐
           │              │
      Terminal         PuTTY
           │              │
           └──────┬───────┘
                  │
                  ▼
          Tapez: permit_join 180
                  │
                  ▼
┌─────────────────────────────────────┐
│         GATEWAY ESP32-C6             │
│                                      │
│  🔓 RÉSEAU OUVERT - 180 sec         │
│  ⏱️  Compte à rebours...             │
│  🔴 Aucun appareil enregistré       │
│                                      │
└─────────────────────────────────────┘
         ✓ Étape 2 réussie
```

**Dans le moniteur série:**
```
>>> permit_join 180
I (xxx) ESP_ZB_GATEWAY: Opening network for 180 seconds...
I (xxx) ESP_ZB_GATEWAY: Network(0x1234) is open for 180 seconds
```

---

## Étape 3: KETOTEK en Mode Appairage

```
┌──────────────────────┐
│    KETOTEK KTF0177    │
│                       │
│  Appuyez 3-5 sec sur │
│   le bouton reset ← ─┐
│                    │ │
│  LED commence à   │ │
│  clignoter rapidement
│                    │ │
│  🔴 🔴 🔴 (clignotant) │ │
│                       │ │
│  Mode appairage ACTIF │ │
└────────────┬──────────┘ │
             │            │
             └────────────┘
         ✓ Étape 3 réussie
```

**État du KETOTEK:**
- ❌ Avant: LED éteinte ou fixe
- ✅ Après: LED clignotante (mode appairage)

---

## Étape 4: Découverte et Enregistrement

```
      Gateway (Coordinateur)
              │
              │ Découverte radio
              │
              ▼
        KETOTEK détecté
              │
              │ Handshake Zigbee
              │
              ▼
    ┌─────────────────────┐
    │ Device Announcement │
    └────────┬────────────┘
             │
             ▼
    Enregistrement dans
    g_paired_devices[]
             │
             ▼
    Message dans console:
    "Device registered"
```

**Dans le moniteur série (1-5 secondes après):**
```
I (xxx) ESP_ZB_GATEWAY: New device commissioned or rejoined (short: 0x1234)
I (xxx) ESP_ZB_GATEWAY: Device registered. Total paired devices: 1
```

---

## Étape 5: Vérification

```
                Console
                  △
                  │
           Tapez: list_devices
                  │
                  ▼
        ┌─────────────────────┐
        │ === Paired Devices ===
        │ Device 1: Short Address=0x1234
        │ Total: 1 device(s)
        └─────────────────────┘
              ✓ Étape 5 réussie
```

**Dans le moniteur série:**
```
>>> list_devices
I (xxx) ESP_ZB_GATEWAY: === Paired Devices ===
I (xxx) ESP_ZB_GATEWAY: Device 1: Short Address=0x1234
I (xxx) ESP_ZB_GATEWAY: Total: 1 device(s)
```

---

## 🎉 Appairage Réussi!

```
┌─────────────────────────────────────┐
│         GATEWAY ESP32-C6             │
│                                      │
│  Device Status:                      │
│  ├─ KETOTEK (0x1234)                │
│  │  ├─ Endpoint: 1                  │
│  │  ├─ Cluster: Thermostat          │
│  │  └─ Status: 🟢 Connected         │
│  │                                   │
│  └─ Ready for data exchange          │
│                                      │
└─────────────────────────────────────┘
        △
        │
        ▼
    Peut envoyer/recevoir:
    - Température
    - Point de consigne
    - Mode système
    - Etc...
```

---

## 🚨 Problèmes Visuels

### ❌ "Network fermé après 180 sec"

```
┌─────────────────────────────────────┐
│         GATEWAY ESP32-C6             │
│                                      │
│  ⏰ 180 secondes écoulées            │
│  🔒 RÉSEAU FERMÉ                    │
│  ❌ Aucun appareil trouvé          │
│                                      │
│  Solution:                          │
│  Tapez: permit_join 180             │
│  Et relancez KETOTEK                │
└─────────────────────────────────────┘
```

### ❌ "KETOTEK ne répond pas"

```
┌─────────────────────────────────────┐
│         GATEWAY ESP32-C6             │
│                                      │
│  Device Status:                      │
│  ├─ KETOTEK (0x1234)                │
│  │  ├─ Endpoint: 1                  │
│  │  ├─ Cluster: Thermostat          │
│  │  └─ Status: 🔴 No Response       │
│  │                                   │
│  Troubleshooting:                   │
│  - Vérifier la portée (< 10m)       │
│  - Éloigner des obstacles            │
│  - Vérifier les piles KETOTEK       │
│  - Redémarrer les appareils         │
└─────────────────────────────────────┘
```

---

## 📊 Timeline Complète

```
Temps (secondes)
│
0   ├─ permit_join 180 (réseau ouvert)
│   │
│   ├─ [Attente KETOTEK]
│   │
5   ├─ 🔴 KETOTEK bouton appairage pressé
│   │
│   ├─ [Découverte radio]
│   │
10  ├─ 🟢 Device commissioned (message)
│   │
│   ├─ list_devices (vérification)
│   │
15  ├─ ✅ KETOTEK visible dans liste
│   │
│   ├─ [Transfert de configuration]
│   │
20  ├─ 🟢 LED KETOTEK devient fixe
│   │
│   ├─ ✅ Appareil prêt à communiquer
│   │
...

180 └─ Réseau fermé automatiquement
```

---

## 📱 État des LEDs

### Gateway (ESP32-C6)

| LED | État | Signification |
|-----|------|---------------|
| 🟢 (verte) | Clignotant | Données Zigbee en cours |
| 🔴 (rouge) | Fixe | Alimentée normalement |
| ⚫ (noir) | Aucune | Gateway normale |

### KETOTEK (KTF0177)

| LED | État | Signification |
|-----|------|---------------|
| 🔴 | Clignotant rapide | Mode appairage ACTIF ⭐ |
| 🔴 | Clignotant lent | Recherche réseau |
| 🟢 | Fixe | Connecté et opérationnel |
| ⚫ | Aucune | Batterie faible ou éteint |

---

## 🎪 Matrice d'État

```
┌─────────────┬──────────────┬────────────────┐
│   GATEWAY   │   KETOTEK    │   RÉSULTAT     │
├─────────────┼──────────────┼────────────────┤
│ ✅ Prête    │ ✅ Appairage │ ✅ Succès      │
│ ✅ Ouverte  │ ❌ Éteint    │ ⚠️  Timeout    │
│ ❌ Fermée   │ ✅ Appairage │ ❌ Fail        │
│ ✅ Prête    │ ✅ Appairé   │ ⚠️  Lenteur    │
└─────────────┴──────────────┴────────────────┘
```

---

## 🔧 Diagnostique Rapide

### ✅ Tout fonctionne
```
1. ✅ Gateway logs visibles
2. ✅ Network formed
3. ✅ permit_join exécuté
4. ✅ KETOTEK LED clignotante
5. ✅ "Device commissioned" affiché
6. ✅ list_devices affiche l'appareil
7. ✅ KETOTEK LED devient fixe
```

### ⚠️  Quelque chose ne va pas
```
1. ✅ Gateway logs visibles
2. ✅ Network formed
3. ✅ permit_join exécuté
4. ✅ KETOTEK LED clignotante
5. ❌ "Device commissioned" NON affiché
   → Voir TROUBLESHOOTING.md
```

---

## 📍 Localisation des Commandes

```
Terminal/Console
     │
     ├─ Via VSCode:
     │  Ctrl+Shift+P > Monitor
     │
     ├─ Via PowerShell:
     │  idf.py -p COM3 monitor
     │
     └─ Via Scripts:
        pairing.bat (Windows)
        pairing.sh (Linux/WSL)
```

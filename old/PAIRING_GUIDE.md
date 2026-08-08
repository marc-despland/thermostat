# Guide d'appairage KETOTEK KTF0177 avec la Gateway Zigbee

## 📋 Prérequis

- **Gateway ESP32-C6** avec firmware mis à jour
- **KETOTEK KTF0177** (capteur/contrôleur Zigbee)
- Port série USB connecté et monitor actif

## 🔄 Processus d'appairage

### Étape 1 : Vérifier que la gateway est prête

1. Ouvrir la connexion série via le moniteur ESP-IDF
2. Vérifier le message de démarrage :
   ```
   I (xxx) ESP_ZB_GATEWAY: Initialize Zigbee stack
   I (xxx) ESP_ZB_GATEWAY: Start network formation
   I (xxx) ESP_ZB_GATEWAY: Formed network successfully
   ```

### Étape 2 : Ouvrir le réseau pour l'appairage

**Option A : Utiliser la commande CLI**

```bash
permit_join 180
```

Cela ouvrira le réseau pendant 180 secondes (3 minutes).

**Option B : Attendre le redémarrage automatique**

Si le périphérique n'est pas en factory reset, la gateway ouvre automatiquement le réseau lors du redémarrage.

**Vérifier l'ouverture du réseau :**
```
I (xxx) ESP_ZB_GATEWAY: Network(0x1234) is open for 180 seconds
```

### Étape 3 : Mettre le KETOTEK en mode appairage

1. **Maintenir le bouton de réinitialisation** du KETOTEK pressé pendant 3-5 secondes
   - Ou suivre les instructions du fabricant (LED clignotante = mode appairage)
2. **Vous avez 180 secondes** pour compléter l'appairage

### Étape 4 : Vérifier l'appairage réussi

Vous devriez voir dans le moniteur :
```
I (xxx) ESP_ZB_GATEWAY: New device commissioned or rejoined (short: 0x1234)
I (xxx) ESP_ZB_GATEWAY: Device registered. Total paired devices: 1
```

### Étape 5 : Lister les appareils appairés

```bash
list_devices
```

Output attendu :
```
I (xxx) ESP_ZB_GATEWAY: === Paired Devices ===
I (xxx) ESP_ZB_GATEWAY: Device 1: Short Address=0x1234
I (xxx) ESP_ZB_GATEWAY: Total: 1 device(s)
```

## 🛠️ Commandes CLI disponibles

| Commande | Description | Exemple |
|----------|-------------|---------|
| `permit_join <durée>` | Ouvrir réseau (sec) | `permit_join 180` |
| `permit_join 0` | Fermer réseau | `permit_join 0` |
| `list_devices` | Lister les appareils | `list_devices` |
| `remove_device <index>` | Supprimer un appareil | `remove_device 1` |
| `help` | Afficher l'aide | `help` |

## ⚠️ Dépannage

### Problème : "Aucun appareil détecté"

1. **Vérifier la portée radio** - minimum 10 mètres en ligne de vue
2. **Vérifier le canal Zigbee** - utilisé par défaut : **canal 25**
   - KETOTEK peut utiliser canal 15 ou 20 par défaut
   - Modifier dans `esp_zigbee_gateway.h` :
   ```c
   #define ESP_ZB_PRIMARY_CHANNEL_MASK (1l << 15)  // Canal 15
   ```

3. **Réinitialiser le KETOTEK** - appui prolongé (10s) sur le bouton

### Problème : "Appareil appairé mais pas de communication"

- Vérifier que le KETOTEK utilise le même **profil HA (Home Automation)**
- Vérifier que l'**endpoint 1** est utilisé
- Augmenter la puissance du signal si possible

### Problème : "Réseau fermé après timeout"

- Utiliser `permit_join 180` pour réouvrir le réseau
- Puis relancer l'appairage du KETOTEK

## 📡 Propriétés du KETOTEK KTF0177

| Propriété | Valeur |
|-----------|--------|
| Type de cluster | Thermostat (0x0201) |
| Profil | HA (Home Automation) |
| Attributs | Température, Point de consigne de chauffage, Mode système |
| Canal par défaut | 15, 20, ou 25 |

## 🔌 Structure du réseau Zigbee

```
┌─────────────────────────────┐
│   Gateway (Coordinateur)    │
│   ESP32-C6 - Endpoint 1     │
│   Adresse: 0x0000           │
└─────────────────┬───────────┘
                  │
                  │ (Zigbee Mesh)
                  │
        ┌─────────┴────────┐
        │                  │
   ┌────▼────┐        ┌────▼────┐
   │ KETOTEK  │        │ Autres   │
   │ 0x1234   │        │ appareils │
   │ EP 1     │        │          │
   └──────────┘        └──────────┘
```

## ✅ Checklist d'appairage

- [ ] Gateway allumée et prête
- [ ] `permit_join 180` exécuté
- [ ] KETOTEK en mode appairage (LED clignotante)
- [ ] Message "New device commissioned" affiché
- [ ] `list_devices` affiche le KETOTEK
- [ ] Adresse courte (short address) visible

## 📚 Références

- [ESP-IDF Zigbee Gateway](https://docs.espressif.com/projects/esp-zigbee-sdk)
- [KETOTEK KTF0177 Datasheet](https://www.ketotek.com)
- [Profil HA Zigbee](https://zigbeealliance.org)

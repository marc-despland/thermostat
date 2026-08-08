# Dépannage - Appairage KETOTEK avec Gateway Zigbee

## 📊 Diagnostic d'Appairage

### 1. Vérifier la gateway

```bash
# Via l'extension ESP-IDF dans VSCode
idf.py -p COM3 monitor

# Ou via PowerShell
idf.py -p COM3 flash monitor
```

**Signes de bon fonctionnement:**
```
I (xxx) ESP_ZB_GATEWAY: Initialize Zigbee stack
I (xxx) ESP_ZB_GATEWAY: Formed network successfully (Extended PAN ID: ...)
I (xxx) ESP_ZB_GATEWAY: Network(0x1234) is open for 180 seconds
```

### 2. Vérifier le KETOTEK

**Configuration KETOTEK par défaut:**
- Modèle: KTF0177
- Type: Thermostat Zigbee
- Canaux supportés: 15, 20, 25
- Tension: 2.4 - 3.3V (utiliser piles AA)

**Vérifier l'état:**
- LED clignotante = Mode appairage actif
- LED fixe = Appareil appairé
- Pas de LED = Batterie faible ou défaut

## 🔧 Problèmes Courants et Solutions

### ❌ Problème 1: "Network formation failed"

**Symptôme:**
```
E (xxx) ESP_ZB_GATEWAY: Failed to initialize Zigbee stack
```

**Solutions:**
1. **Réinitialiser la NV-RAM:**
   ```bash
   idf.py -p COM3 erase-flash
   ```

2. **Vérifier le canal Zigbee** dans `esp_zigbee_gateway.h`:
   ```c
   #define ESP_ZB_PRIMARY_CHANNEL_MASK (1l << 25)
   ```
   Essayer les canaux: 15, 20, 25

3. **Vérifier la version du firmware ESP-IDF** (minimum 5.0.0):
   ```bash
   idf.py --version
   ```

### ❌ Problème 2: "Device not responding after pairing"

**Symptôme:**
- L'appareil apparaît dans `list_devices`
- Mais ne communique pas

**Solutions:**

1. **Vérifier la portée:**
   - Minimum 10 mètres en ligne de vue
   - Éloigner des obstacles métalliques
   - Tester plus près d'abord (< 2 mètres)

2. **Rébâtir la liaison réseau:**
   ```bash
   permit_join 0        # Fermer le réseau
   permit_join 180      # Réouvrir
   # Relancer appairage KETOTEK
   ```

3. **Vérifier les attributs du cluster thermostat:**
   ```c
   // Dans esp_zigbee_gateway.c - zb_attribute_handler
   if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT) {
       ESP_LOGI(TAG, "Thermostat attribute: 0x%x = %d",
                message->attribute.id, 
                *(int16_t *)message->attribute.data.value);
   }
   ```

### ❌ Problème 3: "Permit join timeout"

**Symptôme:**
```
W (xxx) ESP_ZB_GATEWAY: Network(0x1234) closed, devices joining not allowed.
```

**Solutions:**

1. **Réouvrir le réseau:**
   ```bash
   permit_join 180
   ```

2. **Augmenter la durée:**
   ```bash
   permit_join 300  # 5 minutes au lieu de 3
   ```

3. **Redémarrer la gateway:**
   - Redémarrage logiciel ou physique
   - La gateway devrait réouvrir le réseau automatiquement

### ❌ Problème 4: "Port COM not available"

**Symptôme:**
```
Error: could not open port 'COM3': [Errno 2] File not found: 'COM3'
```

**Solutions:**

1. **Identifier le bon port:**
   ```powershell
   # PowerShell
   Get-CimInstance Win32_SerialPort | Select-Object Name, Description
   ```

2. **Vérifier dans le Gestionnaire de périphériques:**
   - Démarrer > Gestionnaire de périphériques
   - Développer "Ports (COM & LPT)"
   - Chercher "USB JTAG" ou "Silicon Labs"

3. **Utiliser un câble USB différent** (le câble peut être défaillant)

### ❌ Problème 5: "Cannot open console REPL"

**Symptôme:**
```
E (xxx) Failed to initialize console
```

**Solutions:**

1. **Décommenter CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG** dans sdkconfig:
   ```bash
   idf.py menuconfig
   # Component config > ESP System Settings > Channel for console output
   # Sélectionner "USB Serial JTAG Controller"
   ```

2. **Installer les drivers USB:**
   - Silicon Labs CP210x drivers (si utilisant ce contrôleur)
   - Ou drivers CH340 (selon la carte)

3. **Recompiler et reflasher:**
   ```bash
   idf.py clean
   idf.py build
   idf.py -p COM3 flash monitor
   ```

## 📈 Escalade Avancée

### Activer les logs détaillés Zigbee

Dans `sdkconfig` ou `menuconfig`:
```bash
idf.py menuconfig
# Component config > Zigbee > Logging level
# Sélectionner "Debug"
```

Recompiler et observer les logs détaillés:
```
D (xxx) ZB_STATE: ... (messages détaillés)
```

### Tracer les paquets réseau

Ajouter dans `esp_zigbee_gateway.c`:
```c
/* Dans zb_action_handler */
ESP_LOGD(TAG, "Received packet from 0x%04hx ep:%d cluster:0x%x attr:0x%x",
         message->info.src_addr, message->info.src_endpoint,
         message->info.cluster, message->attribute.id);
```

### Analyser les performances radio

```bash
# Vérifier la qualité du signal et les retransmissions
idf.py monitor
# Dans les logs, chercher "Link quality indicator (LQI)"
```

## 🔍 Commandes de diagnostic CLI

```bash
# Vérifier l'état du coordonnateur
permit_join 0    # Fermer le réseau

# Lister tous les appareils
list_devices

# Vérifier les statistiques (si implémentées)
info             # Afficher infos gateway

# Redémarrer la gateway (soft reset)
restart          # Ou utilisez le bouton physique

# Réinitialiser complètement (factory reset)
factory_reset    # Attention: supprime tous les appareils
```

## 📞 Support

Si les problèmes persistent:

1. **Collecteur les logs complets:**
   ```bash
   idf.py -p COM3 monitor > logs.txt
   # Laisser tourner 2-3 minutes avec appairage KETOTEK
   ```

2. **Vérifier:**
   - Version firmware KETOTEK (contacter KETOTEK)
   - Version ESP-IDF (minimum 5.0.0)
   - Certificat Zigbee du KETOTEK

3. **Consulter:**
   - [Docs ESP-Zigbee](https://docs.espressif.com/projects/esp-zigbee-sdk)
   - [Specs Zigbee Alliance](https://zigbeealliance.org)

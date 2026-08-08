


# Documentation Thermostat ESP32-C6

## Table des matières
1. [Matériel](#materiel)
2. [Compilation et Déploiement](#compilation-et-deploiement)
3. [Configuration de l'environnement](#configuration-de-lenvironnement)

---

## Materiel 

### ESP32-C6-ZERO

* [Documentation](https://www.waveshare.com/wiki/ESP32-C6-Zero)
* [Installation pour vscode](https://www.waveshare.com/wiki/ESP32-C6-Zero#Install_VSCode)

### KETOTEK KTF0177

---

## Compilation et Déploiement

### Méthode 1 : Build sous WSL + Flash depuis PowerShell/VSCode

Cette méthode utilise WSL pour la compilation (plus rapide) et Windows pour le flash (accès direct aux ports COM).

#### Étape 1 : Compiler sous WSL

1. **Ouvrir un terminal WSL** dans VSCode ou via le terminal dédié

2. **Sourcer l'environnement ESP-IDF** :
```bash
source ~/esp/esp-idf/export.sh
```

3. **Naviguer vers le projet** :
```bash
cd /mnt/c/Users/NNJL0657/Projects/Stage3/Thermostat
```

4. **Compiler le projet** :
```bash
idf.py build
```

Ou utiliser le script automatisé :
```bash
./build-wsl.sh
```

#### Étape 2 : Flasher depuis PowerShell/VSCode

**Option A : Via l'extension ESP-IDF dans VSCode**

1. Ouvrir la Command Palette (`Ctrl+Shift+P`)
2. Chercher "ESP-IDF: Flash your project"
3. Ou utiliser le bouton "Flash" dans la barre d'outils ESP-IDF
4. Ou utiliser le raccourci : `Ctrl+E F`

**Option B : Via PowerShell**

1. **Ouvrir un terminal PowerShell** dans VSCode

2. **Activer l'environnement ESP-IDF** (si nécessaire) :
```powershell
. $HOME\esp\esp-idf\export.ps1
```

3. **Identifier le port COM** :
   - Vérifier dans le Gestionnaire de périphériques Windows
   - Généralement `COM3`, `COM4`, etc.

4. **Flasher la carte** :
```powershell
idf.py -p COM3 flash
```

5. **Monitorer les logs** (optionnel) :
```powershell
idf.py -p COM3 monitor
```

6. **Flash + Monitor en une commande** :
```powershell
idf.py -p COM3 flash monitor
```

**Option C : Via les commandes ESP-IDF de VSCode**

Utiliser le terminal ESP-IDF intégré dans VSCode qui configure automatiquement l'environnement.

#### Notes importantes

- ⚠️ **Ports WSL vs Windows** : 
  - WSL voit les ports comme `/dev/ttyUSB0`, `/dev/ttyS3`, etc.
  - Windows voit les ports comme `COM3`, `COM4`, etc.
  - Le mapping est : `/dev/ttyS{N}` → `COM{N}` (ex: `/dev/ttyS3` = `COM3`)

- 🔄 **Les fichiers compilés sont partagés** : 
  - Le dossier `build/` créé sous WSL est accessible depuis Windows
  - Pas besoin de recompiler pour flasher depuis Windows

- 🚀 **Performances** : 
  - Compilation sous WSL : **~2-3 minutes**
  - Compilation sous Windows : **~5-10 minutes**

#### Résolution de problèmes

**Problème : Port COM non trouvé**
```powershell
# Lister les ports disponibles
mode
# Ou
Get-WmiObject Win32_SerialPort | Select-Object Name, DeviceID
```

**Problème : Permission denied sur le port**
- Vérifier qu'aucun autre programme n'utilise le port (Arduino IDE, PuTTY, etc.)
- Fermer le monitor série s'il est ouvert

**Problème : Flash échoue**
1. Appuyer sur le bouton BOOT de l'ESP32-C6 pendant le flash
2. Vérifier que le câble USB supporte les données (pas juste l'alimentation)
3. Essayer un autre port USB

---

### Méthode 2 : Tout sous WSL

Pour compiler et flasher directement depuis WSL :

```bash
source ~/esp/esp-idf/export.sh
cd /mnt/c/Users/NNJL0657/Projects/Stage3/Thermostat
idf.py -p /dev/ttyS3 flash monitor
```

---

### Méthode 3 : Tout sous Windows

Pour compiler et flasher directement depuis Windows :

```powershell
. $HOME\esp\esp-idf\export.ps1
idf.py build
idf.py -p COM3 flash monitor
```

---

## Configuration de l'environnement

### Configuration initiale ESP-IDF

```bash
source ~/esp/esp-idf/export.sh
```
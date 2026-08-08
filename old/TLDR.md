# 🎯 APPAIRER KETOTEK - GUIDE FINALE

## ⚡ TL;DR (Trop Long; Pas Lu)

```bash
permit_join 180      # Ouvrir réseau
# Appuyer bouton KETOTEK 3-5 sec
list_devices         # Vérifier
# ✅ FINI!
```

**Temps total:** 5 minutes

---

## 📌 AVANT DE COMMENCER

- [ ] ESP32-C6 connectée via USB
- [ ] KETOTEK avec piles
- [ ] Port COM détecté (ex: COM3)
- [ ] Lire QUICK_START.md (5 min)

---

## 🚀 3 ÉTAPES SEULEMENT

### ✅ Étape 1: Compiler et Flasher (20 min)

**Sous WSL:**
```bash
source ~/esp/esp-idf/export.sh
cd /mnt/c/Users/NNJL0657/Projects/Stage3/Thermostat
idf.py clean
idf.py build
idf.py -p COM3 flash monitor
```

**Ou via VSCode:**
- Ctrl+Shift+P → "ESP-IDF: Flash your project"

### ✅ Étape 2: Ouvrir le Réseau (1 min)

**Dans le moniteur console:**
```bash
permit_join 180
```

**Vous devriez voir:**
```
I (xxx) ESP_ZB_GATEWAY: Network(0x1234) is open for 180 seconds
```

### ✅ Étape 3: Appairer le KETOTEK (3 min)

**Physiquement:**
1. Appuyer et maintenir le bouton du KETOTEK
2. Pendant 3-5 secondes
3. Relâcher quand la LED commence à clignoter 🔴

**Dans la console, vous verrez:**
```
I (xxx) ESP_ZB_GATEWAY: New device commissioned or rejoined (short: 0x1234)
I (xxx) ESP_ZB_GATEWAY: Device registered. Total paired devices: 1
```

### ✅ Étape 4: Vérifier (30 sec)

**Dans le moniteur console:**
```bash
list_devices
```

**Vous devriez voir:**
```
I (xxx) ESP_ZB_GATEWAY: === Paired Devices ===
I (xxx) ESP_ZB_GATEWAY: Device 1: Short Address=0x1234
I (xxx) ESP_ZB_GATEWAY: Total: 1 device(s)
```

---

## ✅ C'EST RÉUSSI!

Vous avez maintenant:
- ✅ Gateway Zigbee fonctionnelle
- ✅ KETOTEK appairé (LED 🟢 fixe)
- ✅ Communication établie
- ✅ Prêt à utiliser

---

## ❓ Ça ne marche pas?

### Problem 1: "Device not found"
→ Consulter **TROUBLESHOOTING.md**

### Problem 2: "Port COM not found"
→ Vérifier dans Device Manager (COM3?)

### Problem 3: "Network closed après 180s"
```bash
permit_join 180      # Réouvrir le réseau
# Recommencer appairage KETOTEK
```

### Problem 4: "KETOTEK LED ne clignote pas"
→ Consulter **TROUBLESHOOTING.md** section LED

---

## 📚 Pour Plus de Détails

| Besoin | Fichier | Temps |
|--------|---------|-------|
| Démarrage | QUICK_START.md | 5 min |
| Appairer | PAIRING_GUIDE.md | 30 min |
| Problème | TROUBLESHOOTING.md | 20 min |
| Visual | VISUAL_GUIDE.md | 10 min |
| Avancé | ADVANCED_INTEGRATION.md | 45 min |

---

## 🎯 Commandes Utiles

```bash
permit_join 180      # Ouvrir réseau 180 sec
permit_join 0        # Fermer réseau immédiatement
list_devices         # Lister appareils
remove_device 1      # Supprimer appareil 1
help                 # Afficher aide
```

---

## 📞 Besoin d'Aide?

1. Consulter **TROUBLESHOOTING.md**
2. Lire **PAIRING_GUIDE.md**
3. Consulter **INDEX.md** pour navigation

---

**Bon appairage! 🎉**

_Pour la documentation complète, voir **00_START_HERE.md**_

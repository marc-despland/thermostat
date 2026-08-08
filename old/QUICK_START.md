# ⚡ Quick Start - Appairage KETOTEK

## 🚀 2 étapes seulement:

### 1️⃣ Compiler et Flasher
```bash
# Sous WSL
source ~/esp/esp-idf/export.sh
cd /mnt/c/Users/NNJL0657/Projects/Stage3/Thermostat
idf.py clean
idf.py build
idf.py -p COM3 flash monitor
```

**Ou via VSCode:**
- `Ctrl+Shift+P` → "ESP-IDF: Flash your project"

### 2️⃣ Appairer KETOTEK
**La gateway ouvre automatiquement le réseau pendant 180 secondes!**

```
[Attendre le log:]
✅ Opening network for 180 seconds - Press KETOTEK pairing button now!

[Dans ces 180 secondes:]
1. Appuyer et maintenir le bouton KETOTEK (3-5 secondes)
2. Relâcher quand la LED commence à clignoter 🔴

[Vous verrez alors:]
✅ Device FOUND! New device commissioned...
✅ Device REGISTERED! Total paired devices: 1
=== PAIRED DEVICES ===
Device 1: Short Address=0x1234
=====================
```

## ✅ C'EST RÉUSSI!

La gateway affiche maintenant tous les appareils appairés dans les logs:
- **LED KETOTEK:** 🟢 Fixe (connectée)
- **Gateway logs:** Affiche liste des appareils

---

## 🎯 Résumé Rapide

| Étape | Action | Temps |
|-------|--------|-------|
| 1 | Compiler + Flasher | 20 min |
| 2 | Appuyer bouton KETOTEK | 1 min |
| 3 | Voir logs d'appairage | 30 sec |

**Total: 21 minutes** ✅

---

## 📱 Logs Attendus

**Au démarrage:**
```
I (xxx) ESP_ZB_GATEWAY: Formed network successfully...
I (xxx) ESP_ZB_GATEWAY: Opening network for 180 seconds - Press KETOTEK pairing button now!
I (xxx) ESP_ZB_GATEWAY: Network(0x1234) is open for 180 seconds
```

**Après appuyer bouton KETOTEK:**
```
I (xxx) ESP_ZB_GATEWAY: ✅ Device FOUND! New device commissioned (short: 0x1234)
I (xxx) ESP_ZB_GATEWAY: ✅ Device REGISTERED! Total paired devices: 1
I (xxx) ESP_ZB_GATEWAY: === PAIRED DEVICES ===
I (xxx) ESP_ZB_GATEWAY: Device 1: Short Address=0x1234
I (xxx) ESP_ZB_GATEWAY: =====================
```

---

## ❓ Ça ne marche pas?

| Problème | Solution |
|----------|----------|
| Aucun log après flash | Vérifier port COM (COM3?) |
| KETOTEK LED ne clignote pas | Appuyer plus longtemps (5 sec) |
| "Network closed" après 180s | Redémarrer la gateway |
| Les logs ne s'affichent pas | Vérifier moniteur série |

**Pour plus d'aide:** Voir [TROUBLESHOOTING.md](TROUBLESHOOTING.md)

---

## 🔗 Prochaines Étapes

- Lire [PAIRING_GUIDE.md](PAIRING_GUIDE.md) pour détails
- Consulter [TROUBLESHOOTING.md](TROUBLESHOOTING.md) si problème
- Voir [INDEX.md](INDEX.md) pour navigation complète

**Bon appairage! 🎊**

# 📖 Index Documentation - Appairage KETOTEK

Bienvenue! Ce fichier vous aide à trouver rapidement la documentation appropriée.

---

## 🎯 Je suis en Urgence (5 min)

### Réponse Rapide: **Appairage en 3 étapes**

```bash
permit_join 180      # Étape 1: Ouvrir réseau
                     # Étape 2: Appuyer bouton KETOTEK 3-5 sec
list_devices         # Étape 3: Vérifier présence
```

**Voir:** → [QUICK_START.md](QUICK_START.md)

---

## 👨‍💼 Je suis un Utilisateur Normal (20 min)

### Scénario: Appairer mon KETOTEK pour la première fois

**Étapes recommandées:**

1. **Lire** → [QUICK_START.md](QUICK_START.md) (3 min)
   - Vue d'ensemble rapide
   - 3 étapes seulement

2. **Compiler et flasher** (15 min)
   - Utiliser VSCode ou PowerShell
   - Voir DOCUMENTATION.md pour détails

3. **Exécuter appairage** (5 min)
   - Ouvrir console/moniteur
   - Exécuter les 3 commandes

4. **Si problème** → [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
   - Section appropriée
   - Solutions rapides

---

## 👨‍🔧 Je suis un Admin/Tech (1h)

### Scénario: Déployer en production et gérer appareils

**Parcours complet recommandé:**

1. **Comprendre le système** (15 min)
   - Lire → [PAIRING_GUIDE.md](PAIRING_GUIDE.md)
   - Architecture du réseau
   - Commandes disponibles

2. **Tests et dépannage** (20 min)
   - Étudier → [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
   - Scenarios de base

3. **Visualiser le flux** (10 min)
   - Consulter → [VISUAL_GUIDE.md](VISUAL_GUIDE.md)
   - Comprendre les étapes visuelles

4. **Implémentation** (15 min)
   - Consulter → [TECHNICAL_FLOW.md](TECHNICAL_FLOW.md)
   - Messages Zigbee
   - State machines

---

## 👨‍💻 Je suis un Développeur (2-3h)

### Scénario: Étendre les fonctionnalités, intégrer systèmes

**Parcours développeur:**

1. **Modifications** (30 min)
   - Lire → [CHANGELOG_PAIRING.md](CHANGELOG_PAIRING.md)
   - Comprendre quoi a changé
   - Analyser esp_zigbee_gateway.c

2. **Architecture technique** (30 min)
   - Étudier → [TECHNICAL_FLOW.md](TECHNICAL_FLOW.md)
   - Diagrammes d'état
   - Messages échangés

3. **Code avancé** (1h)
   - Consulter → [ADVANCED_INTEGRATION.md](ADVANCED_INTEGRATION.md)
   - Exemples d'intégration
   - MQTT, Web APIs
   - NVS persistent storage

4. **Implémentation** (variable)
   - Modifier esp_zigbee_gateway.c
   - Tester modifications
   - Consulter ESP-IDF docs

---

## 🎓 Parcours de Formation

### Formation Débutant (2h)
```
QUICK_START.md (10 min)
    ↓
VISUAL_GUIDE.md (20 min)
    ↓
Compilation & Flash (30 min)
    ↓
Appairage réel (20 min)
    ↓
PAIRING_GUIDE.md - relecture (20 min)
    ↓
Tests CLI (20 min)
```

### Formation Intermédiaire (3h)
```
PAIRING_GUIDE.md (30 min)
    ↓
TROUBLESHOOTING.md (30 min)
    ↓
TECHNICAL_FLOW.md (30 min)
    ↓
Tests multi-appareils (30 min)
    ↓
CHANGELOG_PAIRING.md (20 min)
    ↓
Gestion appareils (20 min)
```

### Formation Avancée (4h)
```
Tous les docs intermédiaires (1h30)
    ↓
ADVANCED_INTEGRATION.md (1h)
    ↓
Code source analysis (1h)
    ↓
Implémentation custom (30 min)
```

---

## 🔍 Par Sujet

### 🚀 Démarrage Rapide
- [QUICK_START.md](QUICK_START.md) - **3 étapes seulement**
- [VISUAL_GUIDE.md](VISUAL_GUIDE.md) - **Approche visuelle**

### 📖 Guide Complet
- [PAIRING_GUIDE.md](PAIRING_GUIDE.md) - **Appairage détaillé**
- [PAIRING_GUIDE.md#propriétés-du-ketotek](PAIRING_GUIDE.md#propriétés-du-ketotek) - **Specs KETOTEK**

### 🔧 Configuration et Commandes
- [PAIRING_GUIDE.md#commandes-cli](PAIRING_GUIDE.md#commandes-cli) - **Commandes disponibles**
- [QUICK_START.md](QUICK_START.md) - **Accès console**

### ❌ Dépannage
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) - **Problèmes et solutions**
- [TROUBLESHOOTING.md#problèmes-courants](TROUBLESHOOTING.md#problèmes-courants) - **5 problèmes principaux**

### 📊 Architecture Technique
- [TECHNICAL_FLOW.md](TECHNICAL_FLOW.md) - **Flux complet**
- [TECHNICAL_FLOW.md#diagramme-principal](TECHNICAL_FLOW.md#diagramme-principal) - **Vue d'ensemble**
- [TECHNICAL_FLOW.md#état-machine-zigbee](TECHNICAL_FLOW.md#état-machine-zigbee) - **State machines**

### 💾 Intégration Avancée
- [ADVANCED_INTEGRATION.md](ADVANCED_INTEGRATION.md) - **Code avancé**
- [ADVANCED_INTEGRATION.md#5-sauvegarde-persistent-nvs](ADVANCED_INTEGRATION.md#5-sauvegarde-persistent-nvs) - **Persistance**
- [ADVANCED_INTEGRATION.md#6-notifications-mqtt](ADVANCED_INTEGRATION.md#6-notifications-mqtt) - **MQTT**

### 📝 Modifications Code
- [CHANGELOG_PAIRING.md](CHANGELOG_PAIRING.md) - **Changements détaillés**
- [CHANGELOG_PAIRING.md#modifications-du-code](CHANGELOG_PAIRING.md#modifications-du-code) - **Code modifié**

### ✅ Vérification
- [CHECKLIST.md](CHECKLIST.md) - **Checklist complète**
- [CHECKLIST.md#checklist-pour-l-utilisateur-final](CHECKLIST.md#checklist-pour-l-utilisateur-final) - **Étapes à suivre**

---

## 🎯 Par Cas d'Usage

### Cas 1: "Comment appairer un KETOTEK?"
→ [QUICK_START.md](QUICK_START.md) + [PAIRING_GUIDE.md](PAIRING_GUIDE.md)

### Cas 2: "L'appairage ne marche pas"
→ [TROUBLESHOOTING.md](TROUBLESHOOTING.md)

### Cas 3: "Je veux modifier le code"
→ [CHANGELOG_PAIRING.md](CHANGELOG_PAIRING.md) + [ADVANCED_INTEGRATION.md](ADVANCED_INTEGRATION.md)

### Cas 4: "Je veux comprendre le flux technique"
→ [TECHNICAL_FLOW.md](TECHNICAL_FLOW.md)

### Cas 5: "Je veux ajouter MQTT/Web API"
→ [ADVANCED_INTEGRATION.md](ADVANCED_INTEGRATION.md)

### Cas 6: "Je dois former l'équipe"
→ [CHECKLIST.md](CHECKLIST.md) (Plan de formation)

### Cas 7: "Je dois valider la configuration"
→ [CHECKLIST.md](CHECKLIST.md) (Vérifications)

---

## 📊 Guide de Navigation

```
Vous êtes ici
    │
    ▼
Quel est votre niveau?
    │
    ├─→ Débutant    → QUICK_START.md
    ├─→ Intermédiaire → PAIRING_GUIDE.md
    └─→ Avancé       → ADVANCED_INTEGRATION.md
                        TECHNICAL_FLOW.md
    
Avez-vous un problème?
    │
    ├─→ Appairage simple → QUICK_START.md
    ├─→ Erreur/bug      → TROUBLESHOOTING.md
    ├─→ Code custom     → ADVANCED_INTEGRATION.md
    └─→ Validation      → CHECKLIST.md
    
Voulez-vous comprendre...?
    │
    ├─→ Les étapes     → VISUAL_GUIDE.md
    ├─→ Le flux Zigbee → TECHNICAL_FLOW.md
    ├─→ Les commandes  → PAIRING_GUIDE.md
    └─→ Les changements→ CHANGELOG_PAIRING.md
```

---

## 🚀 Raccourcis Rapides

### Pour Appairage Rapide
1. Ouvrir terminal
2. Taper: `permit_join 180`
3. Appuyer bouton KETOTEK
4. Taper: `list_devices`

### Pour Dépannage Rapide
- "Device not found" → TROUBLESHOOTING.md#problème-2
- "Network closed" → TROUBLESHOOTING.md#problème-3
- "Port COM not available" → TROUBLESHOOTING.md#problème-4

### Pour Code Custom
1. Lire ADVANCED_INTEGRATION.md
2. Copier exemple approprié
3. Modifier esp_zigbee_gateway.c
4. Compiler: `idf.py build`

---

## 📚 Structure Complète des Docs

```
README_PAIRING.md         ← Vous êtes ici!
├─ QUICK_START.md        ← Démarrer en 3 étapes
├─ PAIRING_GUIDE.md      ← Guide complet détaillé
├─ VISUAL_GUIDE.md       ← Guide visuel ASCII
├─ TROUBLESHOOTING.md    ← Dépannage
├─ TECHNICAL_FLOW.md     ← Flux technique
├─ ADVANCED_INTEGRATION.md ← Code avancé
├─ CHANGELOG_PAIRING.md  ← Modifications
├─ CHECKLIST.md          ← Validation
├─ pairing.sh            ← Script Linux
└─ pairing.bat           ← Script Windows
```

---

## ⏱️ Temps de Lecture Estimé

| Document | Temps | Audience |
|----------|-------|----------|
| QUICK_START.md | 5 min | Tous |
| VISUAL_GUIDE.md | 10 min | Visuels |
| PAIRING_GUIDE.md | 30 min | Utilisateurs |
| TROUBLESHOOTING.md | 20 min | Support |
| TECHNICAL_FLOW.md | 30 min | Développeurs |
| ADVANCED_INTEGRATION.md | 45 min | Développeurs |
| CHANGELOG_PAIRING.md | 10 min | Tech leads |
| CHECKLIST.md | 15 min | Managers |

**Total: 3-4h pour documentation complète**

---

## 🎯 Recommandations par Rôle

### 👤 Utilisateur Final
```
QUICK_START.md
    ↓
VISUAL_GUIDE.md (optionnel)
    ↓
TROUBLESHOOTING.md (si besoin)
```

### 👥 Administrateur Système
```
PAIRING_GUIDE.md
    ↓
TROUBLESHOOTING.md
    ↓
TECHNICAL_FLOW.md
    ↓
CHECKLIST.md
```

### 🔧 Développeur
```
CHANGELOG_PAIRING.md
    ↓
TECHNICAL_FLOW.md
    ↓
ADVANCED_INTEGRATION.md
    ↓
Code source
```

### 📊 Manager/Chef de Projet
```
README_PAIRING.md (résumé)
    ↓
CHECKLIST.md (plan)
    ↓
TECHNICAL_FLOW.md (architecture)
```

---

## 🔗 Ressources Externes

- [ESP-Zigbee SDK Documentation](https://docs.espressif.com/projects/esp-zigbee-sdk)
- [ESP-IDF Getting Started](https://docs.espressif.com/projects/esp-idf)
- [Zigbee Alliance Standards](https://zigbeealliance.org)
- [KETOTEK KTF0177 Datasheet](https://www.ketotek.com)

---

## ❓ FAQ Rapide

**Q: Par où commencer?**
A: [QUICK_START.md](QUICK_START.md)

**Q: Ça ne marche pas, que faire?**
A: [TROUBLESHOOTING.md](TROUBLESHOOTING.md)

**Q: Je veux modifier le code?**
A: [ADVANCED_INTEGRATION.md](ADVANCED_INTEGRATION.md)

**Q: Je dois former l'équipe?**
A: [CHECKLIST.md](CHECKLIST.md)

**Q: Où est la doc technique?**
A: [TECHNICAL_FLOW.md](TECHNICAL_FLOW.md)

---

## 📞 Support

Si vous ne trouvez pas votre réponse:
1. Consulter [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
2. Taper `help` dans la console
3. Contacter l'équipe support

---

**Bon apprentissage! 🎓**

_Dernière mise à jour: 22 janvier 2026_

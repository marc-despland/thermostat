# Analyse Zigbee2MQTT - Saswell/KETOTEK KTF0177

## 📚 Vue d'ensemble

Ce document analyse le code source de zigbee2mqtt pour comprendre comment les commandes sont envoyées au thermostat Saswell/KETOTEK KTF0177.

---

## 🔗 Architecture de communication

### Flow de commande (toZigbee)

```
User input (e.g., current_heating_setpoint = 22.5°C)
    ↓
Converter function (saswell_thermostat_current_heating_setpoint)
    ↓
convertSet() async function
    ↓
Value transformation (22.5 * 10 = 225)
    ↓
sendDataPointValue(entity, DP_ID, value)
    ↓
sendDataPoints(entity, [dpValue], "dataRequest", seq)
    ↓
entity.command("manuSpecificTuya", "dataRequest", {seq, dpValues})
    ↓
ZCL command sent to device (Cluster 0xEF00)
```

### Structure du paquet ZCL

**Source:** `sendDataPoints()` dans `legacy.ts:125`

```typescript
await entity.command(
    "manuSpecificTuya",      // Cluster name
    "dataRequest",           // Command type
    {
        seq,                 // Sequence number (transaction ID)
        dpValues,            // Array of DataPoint values
    },
    {disableDefaultResponse: true}
);
```

**Contenu du paquet :**
- **seq** : uint16 (0x0000 à 0xFFFF) - auto-incrémenté globalement
- **dpValues** : Array d'objets avec structure `{dp, datatype, data}`

---

## 🔍 Composition du DataPoint (dpValue)

### Structure générale

```typescript
interface DpValue {
    dp: number;              // DataPoint ID (0-255)
    datatype: number;        // Type de données (0-5)
    data: Buffer;            // Valeur encodée
}
```

### Types de données (dataTypes)

```typescript
const dataTypes = {
    raw:     0,  // Données brutes [bytes]
    bool:    1,  // Booléen [0/1]
    value:   2,  // Valeur 4-byte big-endian
    string:  3,  // Chaîne N-byte
    enum:    4,  // Énumération [0-255]
    bitmap:  5,  // Bitmap [1,2,4 bytes]
};
```

---

## 🌡️ Exemple concret : Température de setpoint

### Code de l'utilisateur (Zigbee2MQTT)

```typescript
// File: src/lib/legacy.ts:5892
saswell_thermostat_current_heating_setpoint: {
    key: ["current_heating_setpoint"],
    convertSet: async (entity, key, value: any, meta) => {
        const temp = Math.round(value * 10);  // 22.5°C → 225
        await sendDataPointValue(entity, dataPoints.saswellHeatingSetpoint, temp);
    },
} satisfies Tz.Converter,
```

### Transformation des données

```
Input: 22.5°C
    ↓
Math.round(22.5 * 10) = 225
    ↓
sendDataPointValue(entity, 103, 225)
    ↓
dpValueFromIntValue(103, 225)
    ↓
{
    dp: 103,
    datatype: 2,  // "value" type
    data: convertDecimalValueTo4ByteHexArray(225)
}
    ↓
Conversion décimale → hex 4-byte
```

### Conversion décimale vers 4-byte big-endian

**Fonction:** `convertDecimalValueTo4ByteHexArray()` ligne 115

```typescript
function convertDecimalValueTo4ByteHexArray(value: number) {
    const hexValue = Number(value).toString(16).padStart(8, "0");
    const chunk1 = hexValue.substring(0, 2);
    const chunk2 = hexValue.substring(2, 4);
    const chunk3 = hexValue.substring(4, 6);
    const chunk4 = hexValue.substring(6);
    return Buffer.from([chunk1, chunk2, chunk3, chunk4].map((hexVal) => 
        Number.parseInt(hexVal, 16)
    ));
}
```

**Exemple avec 225:**

```
225 (décimal) → "e5" (hex)
Padded to 8 chars: "000000e5"
Chunks:
  - chunk1 = "00" → 0x00
  - chunk2 = "00" → 0x00
  - chunk3 = "00" → 0x00
  - chunk4 = "e5" → 0xE5

Buffer résultant: [0x00, 0x00, 0x00, 0xE5] (big-endian)
```

### Paquet final ZCL

```
seq: 0x0001 (incrémenté globalement)
dpValues: [
    {
        dp: 103,
        datatype: 2,
        data: [0x00, 0x00, 0x00, 0xE5]
    }
]
```

---

## 📊 Tableau des DataPoints pour Saswell SEA801

Source: `src/lib/legacy.ts:699-730`

| DataPoint | ID  | Nom variable | Type | Description |
|-----------|-----|------|------|-------------|
| 3 | `saswellHeating` | État de chauffage | bool (1 byte) | Soupape ouverte/fermée |
| 8 | `saswellWindowDetection` | Détection fenêtre | bool (1 byte) | Activer/désactiver |
| 10 | `saswellFrostDetection` | Détection gel | bool (1 byte) | Activer/désactiver |
| 27 | `saswellTempCalibration` | Calibration température | value (4 bytes) | -6 à +6°C (signed) |
| 40 | `saswellChildLock` | Verrou enfant | bool (1 byte) | LOCK/UNLOCK |
| 101 | `saswellState` | État système | bool (1 byte) | OFF (0) / HEAT (1) |
| 102 | `saswellLocalTemp` | Température locale | value (4 bytes) | Valeur × 10 |
| **103** | `saswellHeatingSetpoint` | **Setpoint chauffage** | **value (4 bytes)** | **Valeur × 10** |
| 104 | `saswellValvePos` | Position soupape | value (4 bytes) | 1-100% |
| 105 | `saswellBatteryLow` | Batterie faible | bool (1 byte) | Oui/Non |
| 106 | `saswellAwayMode` | Mode absence | bool (1 byte) | ON/OFF |
| 107 | `saswellScheduleMode` | Mode programmation | enum (1 byte) | Manuel/Auto |
| 108 | `saswellScheduleEnable` | Activer programmation | bool (1 byte) | ON/OFF |
| 109 | `saswellScheduleSet` | Définir programme | raw (25 bytes) | Planning hebdo |
| 123-129 | `saswellScheduleSunday...Saturday` | Programmes quotidiens | raw (25 bytes) | Planning par jour |

---

## 🔄 Cycle de vie d'une commande

### 1. **Initialisation de séquence**

```typescript
// File: legacy.ts:125-131
async function sendDataPoints(entity, dpValues, cmd = "dataRequest", seq = undefined) {
    if (seq === undefined) {
        if (gSec === undefined) {
            gSec = 0;
        } else {
            gSec++;
            gSec %= 0xffff;  // Wraps around after 65535
        }
        seq = gSec;
    }
```

**Propriétés du sequence number:**
- Global (`gSec`) auto-incrémenté
- Range: 0x0000 à 0xFFFF (16-bit)
- Utilisé pour matcher les réponses du device

### 2. **Formatage du DataPoint**

Selon le type de valeur:

```typescript
// Valeur numérique 4-byte (type 2)
dpValueFromIntValue(dp, value) {
    return {
        dp, 
        datatype: 2,  // "value" type
        data: convertDecimalValueTo4ByteHexArray(value)
    };
}

// Booléen (type 1)
dpValueFromBool(dp, value) {
    return {
        dp,
        datatype: 1,  // "bool" type
        data: Buffer.from([value ? 1 : 0])
    };
}

// Énumération (type 4)
dpValueFromEnum(dp, value) {
    return {
        dp,
        datatype: 4,  // "enum" type
        data: Buffer.from([value])
    };
}
```

### 3. **Envoi du commande ZCL**

```typescript
// File: legacy.ts:133-140
await entity.command(
    "manuSpecificTuya",        // Custom cluster
    cmd as "dataRequest",      // "dataRequest" = 0x00 command
    {
        seq,                   // Transaction ID
        dpValues,              // DataPoint array
    },
    {disableDefaultResponse: true}  // Ignore ZCL default response
);
```

---

## 🧪 Exemples de conversions complètes

### Exemple 1 : Setpoint température 20.5°C

```
Entrée utilisateur: 20.5°C
↓ Convertisseur
20.5 * 10 = 205
↓ sendDataPointValue(entity, 103, 205)
↓ dpValueFromIntValue(103, 205)
↓ convertDecimalValueTo4ByteHexArray(205)

205 décimal = 0xCD hex
Padded: "000000cd"
Chunks: "00", "00", "00", "cd"
↓
Data: [0x00, 0x00, 0x00, 0xCD]

DpValue final:
{
    dp: 103,
    datatype: 2,
    data: Buffer.from([0x00, 0x00, 0x00, 0xCD])
}
```

### Exemple 2 : Verrou enfant activé

```
Entrée utilisateur: child_lock = "LOCK"
↓ Convertisseur (saswell_thermostat_child_lock)
value === "LOCK" → true
↓ sendDataPointBool(entity, 40, true)
↓ dpValueFromBool(40, true)

DpValue final:
{
    dp: 40,
    datatype: 1,
    data: Buffer.from([1])  // true = 0x01
}
```

### Exemple 3 : Mode système "auto" (programmation)

```
Entrée utilisateur: system_mode = "auto"
↓ Convertisseur (saswell_thermostat_mode)
// Envoi 2 commandes:

1ère commande - Activer le chauffage:
   sendDataPointBool(entity, 101, true)
   {dp: 101, datatype: 1, data: Buffer.from([1])}

2ème commande - Activer la programmation (après delay 3s):
   sendDataPointBool(entity, 108, true)
   {dp: 108, datatype: 1, data: Buffer.from([1])}
```

---

## 🔐 Comparaison avec votre code ESP-IDF

### Zigbee2MQTT (JavaScript)

```typescript
const temp = Math.round(22.5 * 10);  // = 225
await sendDataPointValue(entity, 103, temp);
// Paquet ZCL: {seq: X, dpValues: [{dp: 103, datatype: 2, data: [0x00, 0x00, 0x00, 0xE5]}]}
```

### Votre code ESP-IDF (C) - MIS À JOUR

```c
uint32_t temp = 225;  // 22.5 * 10

uint8_t tuya_cmd[] = {
    seq_high, seq_low,           // Sequence number
    0x67,                        // DP_ID = 103 (Saswell Setpoint)
    0x02,                        // DP_TYPE (value)
    0x00, 0x04,                  // Length = 4 bytes
    (temp >> 24) & 0xFF,         // Big-endian encoding
    (temp >> 16) & 0xFF,         
    (temp >> 8) & 0xFF,          
    temp & 0xFF                  
};
```

**Maintenant en accord avec Zigbee2MQTT** ✅
- DP103 (0x67) = saswellHeatingSetpoint
- Encodage identique: Big-endian 4-byte
- Type datatype: `2` (value)

---

## 📋 Convertisseurs Saswell disponibles

Source: `src/lib/legacy.ts:5892-5954`

```typescript
// Setpoint température
saswell_thermostat_current_heating_setpoint
// Transforme: 22.5°C → 225 (value/4byte)

// Mode système
saswell_thermostat_mode
// Transforme: "heat"/"off"/"auto" → 2 commandes
//   1. DP101 bool (state)
//   2. DP108 bool (schedule_enable) - après delay 3s

// Mode absence
saswell_thermostat_away
// Transforme: "ON"/"OFF" → DP106 bool

// Verrou enfant
saswell_thermostat_child_lock
// Transforme: "LOCK"/"UNLOCK" → DP40 bool

// Détection fenêtre
saswell_thermostat_window_detection
// Transforme: "ON"/"OFF" → DP8 bool

// Détection gel
saswell_thermostat_frost_detection
// Transforme: "ON"/"OFF" → DP10 bool

// Anti-calcaire
saswell_thermostat_anti_scaling
// Transforme: "ON"/"OFF" → DP102 bool

// Calibration température
saswell_thermostat_calibration
// Transforme: -6 à +6°C → DP27 value (signed int)
```

---

## ⚙️ Points clés d'implémentation

### 1. **Sequence Number Management**

- Auto-incrémenté globalement (variable `gSec`)
- Range: 0x0000 à 0xFFFF
- Utilisé pour matcher les réponses du device
- **Pour votre ESP-IDF**: Implémenter un counter similaire

### 2. **Big-endian encoding**

- **Type "value"** (datatype 2): 4 bytes big-endian
  - Exemple: 225 = [0x00, 0x00, 0x00, 0xE5]
- **Type "bool"** (datatype 1): 1 byte [0x00 ou 0x01]
- **Type "enum"** (datatype 4): 1 byte [0x00-0xFF]

### 3. **Timing des commandes multiples**

```typescript
// Mode "auto" envoie 2 commandes avec délai:
await sendDataPointBool(entity, 101, true);  // DP state
await utils.sleep(3000);                     // Attendre 3 secondes
await sendDataPointBool(entity, 108, true);  // DP schedule_enable
```

**Raison**: Thermostat Saswell nécessite un délai pour traiter les commandes

### 4. **Désactiver la réponse ZCL par défaut**

```typescript
{disableDefaultResponse: true}
```

Empêche le device de renvoyer une ZCL DefaultResponse, le device envoie plutôt un rapport TUYA.

---

## 🎯 Recommandations pour votre ESP-IDF

1. **Vérifier votre DP pour setpoint**: Vous utilisez DP04, zigbee2mqtt utilise DP103
   - Tester les deux pour confirmer le bon DP

2. **Implémenter un sequence counter** similaire à `gSec`

3. **Utiliser l'encodage big-endian** pour les valeurs 4-byte

4. **Respecter les délais** si vous envoyez plusieurs commandes

5. **Analyser les réponses du device** pour matcher avec le seq number

6. **Considérer un mapping DP local** pour les DataPoints personnalisés de votre OEM

---

## 📖 Fichiers source analysés

- `src/devices/saswell.ts` - Définition du device
- `src/lib/legacy.ts:2192` - Convertisseur `saswell_thermostat` (fromZigbee)
- `src/lib/legacy.ts:5892` - Convertisseurs Saswell (toZigbee)
- `src/lib/legacy.ts:125` - Fonction `sendDataPoints()`


# 📋 Protocole Zigbee TUYA pour Tête Thermostatique

Documentation du protocole TUYA Zigbee pour le contrôle et la configuration de la tête thermostatique KETOTEK.

---

## 📡 Informations Générales

### Cluster TUYA Custom
- **Cluster ID**: `0xEF00` (Cluster propriétaire TUYA)
- **Direction**: Bidirectionnelle (Client ↔ Server)
- **Format**: Commandes basées sur des DataPoints (DP)

### Format des Commandes TUYA

Toutes les commandes TUYA suivent ce format :

```
[SEQ_H] [SEQ_L] [DP_ID] [DP_TYPE] [LEN_H] [LEN_L] [DATA...]
```

| Champ | Taille | Description |
|-------|--------|-------------|
| `SEQ_H` | 1 byte | Numéro de séquence (octet haut) |
| `SEQ_L` | 1 byte | Numéro de séquence (octet bas) |
| `DP_ID` | 1 byte | Identifiant du DataPoint |
| `DP_TYPE` | 1 byte | Type de données (voir tableau) |
| `LEN_H` | 1 byte | Longueur des données (octet haut) |
| `LEN_L` | 1 byte | Longueur des données (octet bas) |
| `DATA` | N bytes | Données du DataPoint |

### Types de Données (DP_TYPE)

| Type | Valeur | Description | Taille typique |
|------|--------|-------------|----------------|
| Bool | `0x01` | Booléen (ON/OFF) | 1 byte |
| Value | `0x02` | Entier 32 bits | 4 bytes |
| String | `0x03` | Chaîne de caractères | Variable |
| Enum | `0x04` | Énumération | 1 byte |
| Bitmap | `0x05` | Bitmap | Variable |

---

## 📤 Commandes d'Envoi (Gateway → Thermostat)

### Commande 0x00: Set DataPoint

**Description**: Envoie une valeur à un DataPoint pour modifier un paramètre.

**Direction**: Client to Server (`ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV`)

**Structure ZCL**:
```c
esp_zb_zcl_custom_cluster_cmd_req_t cmd_req = {
    .zcl_basic_cmd = {
        .dst_addr_u.addr_short = dst_addr,
        .dst_endpoint = dst_endpoint,
        .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
    },
    .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
    .cluster_id = 0xef00,
    .custom_cmd_id = 0x00,
    .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
    .data = {
        .type = ESP_ZB_ZCL_ATTR_TYPE_ARRAY,
        .value = tuya_cmd,
        .size = sizeof(tuya_cmd),
    },
};
```

#### Exemple: Définir la Température de Consigne

**DataPoint**: DP04  
**Valeur**: 10°C → `100` (température × 10)

**Payload**:
```
00 01 04 02 00 04 00 00 00 64
```

Décomposition:
- `00 01` : Séquence = 1
- `04` : DP_ID = 0x04 (Temperature Setpoint)
- `02` : DP_TYPE = 0x02 (Value/Integer)
- `00 04` : Length = 4 bytes
- `00 00 00 64` : Valeur = 100 (10.0°C)

**Code C**:
```c
void tuya_send_set_temperature(uint16_t dst_addr, uint8_t dst_endpoint, int16_t temperature_deg)
{
    static uint16_t tuya_seq = 0;
    int32_t tuya_temp = temperature_deg * 10;  // 10°C → 100
    
    uint8_t tuya_cmd[] = {
        (tuya_seq >> 8) & 0xFF,     // Sequence High
        tuya_seq & 0xFF,            // Sequence Low
        0x04,                       // DP_ID: Temperature Setpoint
        0x02,                       // DP_TYPE: Value (4 bytes)
        0x00, 0x04,                 // Length: 4 bytes
        (tuya_temp >> 24) & 0xFF,   // Data[0]
        (tuya_temp >> 16) & 0xFF,   // Data[1]
        (tuya_temp >> 8) & 0xFF,    // Data[2]
        tuya_temp & 0xFF            // Data[3]
    };
    tuya_seq++;
    
    // Envoyer via esp_zb_zcl_custom_cluster_cmd_req()
}
```

### Commande 0x11: Data Query (Requête de Données)

**Description**: Interroge le thermostat pour obtenir l'état actuel de tous les DataPoints.

**Direction**: Client to Server

**Payload**:
```
[SEQ_H] [SEQ_L]
```

**Exemple**:
```
00 00
```

**Réponse attendue**: Le thermostat répond avec une commande 0x01 ou 0x02 contenant tous les DataPoints.

---

## 📥 Commandes de Réception (Thermostat → Gateway)

### Commande 0x01: Report DataPoint (Rapport périodique)

**Description**: Le thermostat envoie périodiquement l'état de ses DataPoints.

**Direction**: Server to Client

**Structure**: Identique au format général des DataPoints.

### Commande 0x02: Response DataPoint (Réponse à une requête)

**Description**: Réponse du thermostat suite à une commande 0x00 ou 0x11.

**Direction**: Server to Client

**Structure**: Identique au format général des DataPoints.

### Commande 0x24: Time Sync Request

**Description**: Le thermostat demande une synchronisation de l'heure.

**Direction**: Server to Client

**Payload**:
```
[SEQ_H] [SEQ_L]
```

**Réponse recommandée**: Envoyer une commande 0x24 avec l'heure UTC actuelle.

---

## 📊 DataPoints Disponibles

### DP02: Température Actuelle (Lecture seule)

**ID**: `0x02`  
**Type**: `0x04` (Enum)  
**Longueur**: 4 bytes  
**Description**: Température mesurée par le capteur interne du thermostat.

**Format**: Température × 10 (ex: 210 = 21.0°C)

**Exemple de réception**:
```
seq_H seq_L 02 04 00 04 [00 00 00 D2]
                         └─ 210 = 21.0°C
```

**Code de décodage**:
```c
if (dp_id == 0x02 && dp_type == 0x04 && dp_len == 4) {
    int32_t temp = (data[6] << 24) | (data[7] << 16) | (data[8] << 8) | data[9];
    float temperature_celsius = temp / 10.0;
    ESP_LOGI(TAG, "Current Temperature = %.1f°C", temperature_celsius);
}
```

---

### DP03: Mode de Fonctionnement

**ID**: `0x03`  
**Type**: `0x04` (Enum)  
**Longueur**: 1 byte  
**Description**: Mode manuel ou programmation horaire.

**Valeurs**:
- `0x00` : Mode Programmation (Schedule)
- `0x01` : Mode Manuel

**Exemple**:
```
seq_H seq_L 03 04 00 01 [01]
                         └─ Manuel
```

**Envoi** (pour passer en mode manuel):
```c
uint8_t tuya_cmd[] = {
    (tuya_seq >> 8) & 0xFF,
    tuya_seq & 0xFF,
    0x03,           // DP_ID: Mode
    0x04,           // DP_TYPE: Enum
    0x00, 0x01,     // Length: 1 byte
    0x01            // Value: Manual
};
```

---

### DP04: Température de Consigne (Setpoint)

**ID**: `0x04`  
**Type**: `0x02` (Value)  
**Longueur**: 4 bytes  
**Description**: Température cible définie par l'utilisateur.

**Format**: Température × 10 (ex: 185 = 18.5°C)

**Plage**: Typiquement 5°C à 35°C (50 à 350)

**Exemple d'envoi** (régler à 22.5°C):
```
00 05 04 02 00 04 00 00 00 E1
└───┘ └┘ └┘ └───┘ └─────────┘
  │   │  │    │        └─ 225 (22.5°C)
  │   │  │    └─ Longueur: 4 bytes
  │   │  └─ Type: Value
  │   └─ DP04: Setpoint
  └─ Séquence: 5
```

**Code d'envoi**:
```c
int16_t target_temp = 22;  // 22°C
int32_t tuya_temp = target_temp * 10;  // 220

uint8_t tuya_cmd[] = {
    0x00, 0x06,                 // Séquence
    0x04,                       // DP_ID
    0x02,                       // DP_TYPE
    0x00, 0x04,                 // Length
    (tuya_temp >> 24) & 0xFF,   // Big-endian int32
    (tuya_temp >> 16) & 0xFF,
    (tuya_temp >> 8) & 0xFF,
    tuya_temp & 0xFF
};
```

---

### DP04 (variante): État de Chauffe

**ID**: `0x04`  
**Type**: `0x04` (Enum)  
**Longueur**: 1 byte  
**Description**: État actuel du système de chauffage.

⚠️ **Note**: Il y a deux DP04 différents selon le type ! Vérifiez le contexte.

**Valeurs**:
- `0x00` : OFF (Éteint)
- `0x01` : IDLE (En attente)
- `0x02` : ON (Chauffe active)

**Exemple**:
```
seq_H seq_L 04 04 00 01 [02]
                         └─ Chauffage ON
```

---

### DP05: Température Locale

**ID**: `0x05`  
**Type**: `0x02` (Value)  
**Longueur**: 4 bytes  
**Description**: Température mesurée par un capteur externe (si connecté).

**Format**: Température × 10

**Exemple**:
```
seq_H seq_L 05 02 00 04 [00 00 00 D6]
                         └─ 214 = 21.4°C
```

---

### DP07: Verrouillage Enfant (Child Lock)

**ID**: `0x07`  
**Type**: `0x01` (Bool)  
**Longueur**: 1 byte  
**Description**: Active/désactive le verrouillage des boutons.

**Valeurs**:
- `0x00` : OFF (Déverrouillé)
- `0x01` : ON (Verrouillé)

**Exemple d'activation**:
```
00 07 07 01 00 01 [01]
           └───┘  └─ Lock ON
           └─ 1 byte
```

**Code d'envoi**:
```c
uint8_t tuya_cmd[] = {
    0x00, 0x07,     // Séquence
    0x07,           // DP_ID: Child Lock
    0x01,           // DP_TYPE: Bool
    0x00, 0x01,     // Length: 1 byte
    0x01            // Value: ON
};
```

---

### DP1C-DP22: Programmation Horaire Hebdomadaire

**ID**: `0x1C` à `0x22` (7 DataPoints, un par jour)  
**Type**: Variable  
**Longueur**: 25 bytes  
**Description**: Programme horaire pour chaque jour de la semaine.

**Mapping**:
- `0x1C` : Lundi
- `0x1D` : Mardi
- `0x1E` : Mercredi
- `0x1F` : Jeudi
- `0x20` : Vendredi
- `0x21` : Samedi
- `0x22` : Dimanche

**Format des données** (25 bytes):
```
[Période1: 6 bytes] [Période2: 6 bytes] [Période3: 6 bytes] [Période4: 6 bytes] [Config: 1 byte]
```

Chaque période contient:
- **Heure de début** (2 bytes): Heures × 60 + Minutes
- **Température** (4 bytes): Température × 10

**Exemple de décodage**:
```c
if (dp_id >= 0x1c && dp_id <= 0x22 && dp_len == 25) {
    uint8_t day = dp_id - 0x1b;  // 1=Lundi, 7=Dimanche
    ESP_LOGI(TAG, "Weekly Schedule Day %d", day);
    
    for (int i = 0; i < 4; i++) {
        uint16_t time_minutes = (data[6 + i*6] << 8) | data[7 + i*6];
        int32_t temp = (data[8 + i*6] << 24) | (data[9 + i*6] << 16) | 
                       (data[10 + i*6] << 8) | data[11 + i*6];
        
        uint8_t hour = time_minutes / 60;
        uint8_t minute = time_minutes % 60;
        
        ESP_LOGI(TAG, "  Period %d: %02d:%02d → %.1f°C", 
                 i+1, hour, minute, temp/10.0);
    }
}
```

---

## 🔧 Exemples Pratiques

### 1. Lire tous les DataPoints

```c
void tuya_query_all_datapoints(uint16_t dst_addr, uint8_t dst_endpoint)
{
    static uint16_t tuya_seq = 0;
    uint8_t tuya_cmd[] = {
        (tuya_seq >> 8) & 0xFF,
        tuya_seq & 0xFF
    };
    tuya_seq++;
    
    esp_zb_zcl_custom_cluster_cmd_req_t cmd_req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = dst_addr,
            .dst_endpoint = dst_endpoint,
            .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .cluster_id = 0xef00,
        .custom_cmd_id = 0x11,  // Data Query
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        .data = {
            .type = ESP_ZB_ZCL_ATTR_TYPE_ARRAY,
            .value = tuya_cmd,
            .size = sizeof(tuya_cmd),
        },
    };
    
    esp_zb_zcl_custom_cluster_cmd_req(&cmd_req);
}
```

### 2. Définir la température à 19.5°C

```c
void tuya_set_temperature_19_5(uint16_t dst_addr, uint8_t dst_endpoint)
{
    static uint16_t tuya_seq = 0;
    int32_t temp = 195;  // 19.5°C × 10
    
    uint8_t tuya_cmd[] = {
        (tuya_seq >> 8) & 0xFF,
        tuya_seq & 0xFF,
        0x04,           // DP_ID: Setpoint
        0x02,           // DP_TYPE: Value
        0x00, 0x04,     // Length
        (temp >> 24) & 0xFF,
        (temp >> 16) & 0xFF,
        (temp >> 8) & 0xFF,
        temp & 0xFF
    };
    tuya_seq++;
    
    // Envoyer avec cmd_id = 0x00
}
```

### 3. Activer le verrouillage enfant

```c
void tuya_enable_child_lock(uint16_t dst_addr, uint8_t dst_endpoint)
{
    static uint16_t tuya_seq = 0;
    
    uint8_t tuya_cmd[] = {
        (tuya_seq >> 8) & 0xFF,
        tuya_seq & 0xFF,
        0x07,           // DP_ID: Child Lock
        0x01,           // DP_TYPE: Bool
        0x00, 0x01,     // Length: 1
        0x01            // Value: ON
    };
    tuya_seq++;
    
    // Envoyer avec cmd_id = 0x00
}
```

### 4. Passer en mode manuel

```c
void tuya_set_manual_mode(uint16_t dst_addr, uint8_t dst_endpoint)
{
    static uint16_t tuya_seq = 0;
    
    uint8_t tuya_cmd[] = {
        (tuya_seq >> 8) & 0xFF,
        tuya_seq & 0xFF,
        0x03,           // DP_ID: Mode
        0x04,           // DP_TYPE: Enum
        0x00, 0x01,     // Length: 1
        0x01            // Value: Manual
    };
    tuya_seq++;
    
    // Envoyer avec cmd_id = 0x00
}
```

---

## 📝 Notes Importantes

### Numéros de Séquence
- Incrémentez le numéro de séquence à chaque commande envoyée
- Le thermostat peut utiliser ce numéro pour associer les réponses aux requêtes
- Format: 16 bits (big-endian)

### Encodage Big-Endian
- Toutes les valeurs multi-bytes sont en **big-endian**
- Longueurs, températures, temps sont encodés MSB first

### Temporisation
- Attendez au moins 1-2 secondes entre les commandes
- Le thermostat peut ne pas répondre immédiatement
- Utilisez des timers pour espacer les commandes répétées

### Réception Multiple
- Certaines commandes peuvent générer plusieurs réponses
- Un DataPoint peut apparaître plusieurs fois avec des types différents
- Vérifiez toujours le `DP_TYPE` en plus du `DP_ID`

### Vérifications Avant Envoi
```c
// Vérifier que la gateway est connectée au réseau
if (!esp_zb_bdb_dev_joined()) {
    ESP_LOGE(TAG, "Gateway not joined to network");
    return ESP_ERR_INVALID_STATE;
}

// Vérifier que l'adresse du device est valide
if (dst_addr == 0 || dst_addr == 0xFFFF) {
    ESP_LOGE(TAG, "Invalid device address");
    return ESP_ERR_INVALID_ARG;
}
```

---

## 🐛 Dépannage

### La commande échoue avec "ERROR"
- ✅ Vérifiez que la gateway est jointe au réseau (`esp_zb_bdb_dev_joined()`)
- ✅ Vérifiez que l'adresse courte du device est correcte
- ✅ Vérifiez que l'endpoint est correct (généralement 1)
- ✅ Assurez-vous que le device est toujours appairé et actif

### Le thermostat ne répond pas
- ⏱️ Attendez quelques secondes, le thermostat peut être occupé
- 🔋 Vérifiez que les piles ne sont pas faibles
- 📡 Vérifiez la portée Zigbee (max ~10m en intérieur)
- 🔄 Essayez d'envoyer une commande 0x11 (Data Query) pour "réveiller" le device

### Valeurs de température incorrectes
- 🔢 Rappelez-vous : température × 10 (ex: 22.5°C = 225)
- 📏 Format: int32 big-endian sur 4 bytes
- 🌡️ Plage typique : 50-350 (5.0°C - 35.0°C)

### Confusion entre les DP04
- ⚠️ DP04 existe en deux versions : Type 0x02 (Setpoint) et Type 0x04 (État)
- 🔍 Vérifiez toujours le `DP_TYPE` pour différencier

---

## 📚 Références

- **ESP-Zigbee SDK**: [Espressif Zigbee Documentation](https://docs.espressif.com/projects/esp-zigbee-sdk/)
- **TUYA Protocol**: Protocole propriétaire, reverse-engineered
- **Cluster 0xEF00**: Custom cluster TUYA standard

---

**Dernière mise à jour**: 23 janvier 2026  
**Version**: 1.0  
**Auteur**: Documentation générée depuis l'analyse du code ESP32-C6 Gateway

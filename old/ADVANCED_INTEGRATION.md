# 🔌 Intégration Avancée KETOTEK

Ce document montre comment intégrer plus profondément le KETOTEK avec la gateway.

## 1. Lecture des Attributs Thermostat

### Attribute IDs du KETOTEK

```c
// Dans zb_attribute_handler
switch(message->attribute.id) {
    case ESP_ZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_ID:
        // 0x0000 - Température locale (hundredths of °C)
        int16_t temp = *(int16_t *)message->attribute.data.value;
        ESP_LOGI(TAG, "KETOTEK Temperature: %.2f°C", temp / 100.0f);
        break;
        
    case ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID:
        // 0x0012 - Point de consigne chauffage (hundredths of °C)
        int16_t setpoint = *(int16_t *)message->attribute.data.value;
        ESP_LOGI(TAG, "KETOTEK Heating Setpoint: %.2f°C", setpoint / 100.0f);
        break;
        
    case ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID:
        // 0x001C - Mode système
        uint8_t mode = *(uint8_t *)message->attribute.data.value;
        const char *mode_str[] = {"Off", "Heat", "Cool", "Auto"};
        ESP_LOGI(TAG, "KETOTEK Mode: %s", mode < 4 ? mode_str[mode] : "Unknown");
        break;
        
    case ESP_ZB_ZCL_ATTR_THERMOSTAT_CONTROL_SEQUENCE_OF_OPERATION_ID:
        // 0x001B - Séquence de contrôle
        uint8_t seq = *(uint8_t *)message->attribute.data.value;
        ESP_LOGI(TAG, "KETOTEK Control Sequence: %d", seq);
        break;
}
```

## 2. Écriture des Attributs (Commandes)

### Envoyer une Température de Consigne

```c
/* Fonction pour modifier le point de consigne du KETOTEK */
static void set_ketotek_heating_setpoint(uint16_t short_addr, int16_t temp_hundredths) {
    esp_zb_zcl_set_attr_value_message_t msg = {
        .info.profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .info.cluster = ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
        .info.cluster_role = ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE,
        .info.src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        .info.dst_endpoint = 1,  // Endpoint du KETOTEK
        .info.dst_addr = short_addr,
        .attribute.id = ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID,
        .attribute.data.type = ESP_ZB_ZCL_ATTR_TYPE_INT16,
        .attribute.data.size = 2,
        .attribute.data.value = &temp_hundredths,
    };
    
    esp_zb_zcl_set_attr_value(&msg);
    ESP_LOGI(TAG, "Sent heating setpoint %.2f°C to KETOTEK (0x%04x)", 
             temp_hundredths / 100.0f, short_addr);
}

/* Exemple d'utilisation */
// int16_t desired_temp = 2200;  // 22°C
// set_ketotek_heating_setpoint(0x1234, desired_temp);
```

### Envoyer un Mode Système

```c
static void set_ketotek_system_mode(uint16_t short_addr, uint8_t mode) {
    // mode: 0=Off, 1=Heat, 2=Cool, 3=Auto, 4=Heat+Cool
    
    esp_zb_zcl_set_attr_value_message_t msg = {
        .info.profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .info.cluster = ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
        .info.cluster_role = ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE,
        .info.src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        .info.dst_endpoint = 1,  // Endpoint du KETOTEK
        .info.dst_addr = short_addr,
        .attribute.id = ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID,
        .attribute.data.type = ESP_ZB_ZCL_ATTR_TYPE_ENUM8,
        .attribute.data.size = 1,
        .attribute.data.value = &mode,
    };
    
    esp_zb_zcl_set_attr_value(&msg);
    
    const char *mode_str[] = {"Off", "Heat", "Cool", "Auto", "Heat+Cool"};
    ESP_LOGI(TAG, "Sent mode %s to KETOTEK (0x%04x)", 
             mode < 5 ? mode_str[mode] : "Unknown", short_addr);
}

/* Exemples d'utilisation */
// set_ketotek_system_mode(0x1234, 1);  // Mode chauffage
// set_ketotek_system_mode(0x1234, 0);  // Arrêt
```

## 3. Rapports Périodiques (Polling)

### Activer les Rapports Automatiques

```c
/* Fonction pour configurer les rapports automatiques du KETOTEK */
static void enable_ketotek_reporting(uint16_t short_addr) {
    // Configuration des rapports pour l'attribut température
    esp_zb_zcl_config_report_record_t record = {
        .endpoint = 1,
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
        .attribute_id = ESP_ZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_ID,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SERVER,
        .attrType = ESP_ZB_ZCL_ATTR_TYPE_INT16,
        .min_interval = 30,    // Min 30 secondes
        .max_interval = 300,   // Max 5 minutes
        .reportable_change = 50,  // Changement ≥ 0.50°C
    };
    
    esp_zb_zcl_send_configure_reporting_command(&record, 1, short_addr, 1);
    ESP_LOGI(TAG, "Enabled auto-reporting for KETOTEK (0x%04x)", short_addr);
}

/* Appeler une fois après appairage */
// enable_ketotek_reporting(0x1234);
```

## 4. Stockage Persistent (NVS)

### Sauvegarder les Appareils Appairés

```c
#include "nvs_flash.h"

/* Initialiser NVS */
void init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

/* Sauvegarder liste des appareils */
void save_devices_to_nvs(void) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("devices", NVS_READWRITE, &nvs_handle));
    
    // Sauvegarder le nombre d'appareils
    ESP_ERROR_CHECK(nvs_set_u8(nvs_handle, "count", g_paired_devices_count));
    
    // Sauvegarder chaque appareil
    for (int i = 0; i < g_paired_devices_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "device_%d", i);
        ESP_ERROR_CHECK(nvs_set_blob(nvs_handle, key, &g_paired_devices[i], 
                                     sizeof(zb_device_t)));
    }
    
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Devices saved to NVS");
}

/* Charger liste des appareils au démarrage */
void load_devices_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("devices", NVS_READONLY, &nvs_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No devices saved in NVS");
        return;
    }
    
    uint8_t count;
    if (nvs_get_u8(nvs_handle, "count", &count) == ESP_OK) {
        g_paired_devices_count = count;
        
        for (int i = 0; i < count && i < MAX_PAIRED_DEVICES; i++) {
            char key[16];
            snprintf(key, sizeof(key), "device_%d", i);
            size_t size = sizeof(zb_device_t);
            nvs_get_blob(nvs_handle, key, &g_paired_devices[i], &size);
        }
        
        ESP_LOGI(TAG, "Loaded %d devices from NVS", count);
    }
    
    nvs_close(nvs_handle);
}
```

## 5. Interface Web JSON (Optionnel)

### Handler HTTP pour Contrôler les Appareils

```c
#include "esp_http_server.h"

/* GET /api/devices - Lister les appareils */
static esp_err_t get_devices_handler(httpd_req_t *req) {
    char buffer[512];
    int pos = 0;
    
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "{\"devices\":[");
    
    for (int i = 0; i < g_paired_devices_count; i++) {
        if (i > 0) pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",");
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, 
                       "{\"id\":%d,\"addr\":\"0x%04x\",\"ep\":%d}",
                       i + 1, g_paired_devices[i].short_addr, 
                       g_paired_devices[i].endpoint);
    }
    
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "],\"count\":%d}", 
                   g_paired_devices_count);
    
    httpd_resp_send(req, buffer, -1);
    return ESP_OK;
}

/* POST /api/devices/{id}/setpoint - Modifier point de consigne */
// Implémentation similaire avec parsing JSON
```

## 6. Notifications MQTT (Optionnel)

### Publier État du KETOTEK

```c
#include "mqtt_client.h"

/* Callback pour publier changements de température */
static void publish_temperature_change(uint16_t short_addr, int16_t temp) {
    char topic[64];
    char payload[32];
    
    snprintf(topic, sizeof(topic), "zigbee/devices/0x%04x/temperature", short_addr);
    snprintf(payload, sizeof(payload), "%.2f", temp / 100.0f);
    
    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 1, false);
}

/* Callback pour recevoir commandes MQTT */
static void mqtt_set_temperature(uint16_t short_addr, float temp_celsius) {
    int16_t temp_hundredths = (int16_t)(temp_celsius * 100);
    set_ketotek_heating_setpoint(short_addr, temp_hundredths);
}
```

## 7. Exemple de Configuration Complète

```c
/* esp_zigbee_gateway.c - Intégration complète */

void init_ketotek_support(void) {
    // Initialiser la persistance
    init_nvs();
    load_devices_from_nvs();
    
    // Charger les appareils enregistrés
    ESP_LOGI(TAG, "Loaded %d devices from storage", g_paired_devices_count);
    
    // Configurer les rapports pour chaque appareil connu
    for (int i = 0; i < g_paired_devices_count; i++) {
        enable_ketotek_reporting(g_paired_devices[i].short_addr);
    }
}

void device_announced_handler(uint16_t short_addr) {
    // Nouveau appareil détecté
    register_device(short_addr);
    
    // Configurer le reporting
    enable_ketotek_reporting(short_addr);
    
    // Sauvegarder la liste
    save_devices_to_nvs();
    
    // Publier l'événement
    if (mqtt_enabled) {
        esp_mqtt_client_publish(mqtt_client, "zigbee/events/joined", 
                               "New device paired", 0, 1, false);
    }
}

/* Dans esp_zb_task() ou app_main() */
// init_ketotek_support();
```

## 📚 Ressources Utiles

- [Cluster Thermostat Zigbee](https://zigbeealliance.org/standards/zigbee-cluster-library/)
- [KETOTEK KTF0177 Datasheet](https://www.ketotek.com)
- [ESP-Zigbee SDK API](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/api-reference/)
- [Zigbee Home Automation Profile](https://zigbeealliance.org)

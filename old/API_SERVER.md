# Implémentation d'une API REST sur ESP32

## 1. **Utiliser le serveur HTTP intégré (ESP-IDF)**

L'ESP-IDF fournit `esp_http_server` - c'est l'approche la plus simple. Ajoutez dans votre `main/CMakeLists.txt` :

```cmake
idf_component_register(SRCS "esp_zigbee_gateway.c" HEADER_DIRS "." REQUIRES esp_http_server)
```

## 2. **Implémenter des handlers HTTP**

Exemple basique dans `main/esp_zigbee_gateway.c` :

```c
#include "esp_http_server.h"

// Handler GET exemple
static esp_err_t get_temperature_handler(httpd_req_t *req) {
    const char resp[] = "{\"temperature\": 22.5, \"humidity\": 45}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Démarrer le serveur
static void start_http_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 1;
    httpd_handle_t server = NULL;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // Enregistrer les routes
        httpd_uri_t uri_get_temp = {
            .uri = "/api/temperature",
            .method = HTTP_GET,
            .handler = get_temperature_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_get_temp);
        ESP_LOGI("HTTP", "Serveur démarré sur port 80");
    }
}
```

## 3. **Configurer WiFi**

```c
#include "esp_wifi.h"

void wifi_init() {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    // Connecter au WiFi avec credentials
    esp_wifi_connect();
}
```

## 4. **Configurer via sdkconfig**

Dans `sdkconfig` ou menu `idf.py menuconfig` :
- Component config → ESP HTTP Server → Enable
- Wireless Networking → WiFi

## 5. **Routes courantes pour un thermostat**

```c
POST /api/temperature/set   // Définir température
GET  /api/status            // État actuel
GET  /api/history          // Historique
POST /api/config           // Configuration
```

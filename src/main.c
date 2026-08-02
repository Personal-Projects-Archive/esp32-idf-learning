#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "mqtt_client.h"

// WiFi Wokwi
#define WIFI_SSID           "Wokwi-GUEST"
#define WIFI_PASS           ""
#define WIFI_CONNECTED_BIT  BIT0

// Broker public gratuit — pas besoin de compte
#define BROKER_URI   "mqtt://broker.hivemq.com:1883"

// Topics — préfixe unique pour éviter les conflits avec d'autres utilisateurs
#define TOPIC_LED    "esp32-idf-learning/led"          // subscribe → reçoit ON/OFF
#define TOPIC_STATUS "esp32-idf-learning/status"       // publish  → état LED
#define TOPIC_TEMP   "esp32-idf-learning/temperature"  // publish  → température simulée

#define LED_PIN  GPIO_NUM_2

static const char *TAG = "MQTT";
static EventGroupHandle_t       wifi_event_group;
static esp_mqtt_client_handle_t mqtt_client;
static bool led_state = false;

// ─── LED ─────────────────────────────────────────────────────────────────────

static void set_led(bool state)
{
    led_state = state;
    gpio_set_level(LED_PIN, state ? 1 : 0);
    ESP_LOGI(TAG, "LED %s", state ? "ON" : "OFF");
}

// ─── Publish ─────────────────────────────────────────────────────────────────

static void publish_status(void)
{
    char msg[32];
    snprintf(msg, sizeof(msg), "{\"led\":\"%s\"}", led_state ? "ON" : "OFF");
    esp_mqtt_client_publish(mqtt_client, TOPIC_STATUS, msg, 0, 1, 0);
    ESP_LOGI(TAG, "Pub %s -> %s", TOPIC_STATUS, msg);
}

// Tâche FreeRTOS — publie une température simulée toutes les 5 secondes
static void publish_task(void *arg)
{
    int tick = 0;
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));

        // Température simulée entre 20.0 et 24.5°C
        float temp = 20.0f + (float)(tick % 10) * 0.5f;
        char msg[16];
        snprintf(msg, sizeof(msg), "%.1f", temp);

        esp_mqtt_client_publish(mqtt_client, TOPIC_TEMP, msg, 0, 0, 0);
        ESP_LOGI(TAG, "Pub %s -> %s°C", TOPIC_TEMP, msg);
        tick++;
    }
}

// ─── MQTT Events ─────────────────────────────────────────────────────────────

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connecte au broker");
            // S'abonne au topic LED dès la connexion
            esp_mqtt_client_subscribe(mqtt_client, TOPIC_LED, 1);
            ESP_LOGI(TAG, "Subscribe: %s", TOPIC_LED);
            publish_status();
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Deconnecte — reconnexion automatique...");
            break;

        case MQTT_EVENT_DATA:
        {
            // Extraction topic et payload (pas null-terminés par défaut)
            char topic[64] = {0};
            char data[64]  = {0};
            int tlen = event->topic_len < 63 ? event->topic_len : 63;
            int dlen = event->data_len  < 63 ? event->data_len  : 63;
            memcpy(topic, event->topic, tlen);
            memcpy(data,  event->data,  dlen);

            ESP_LOGI(TAG, "Recu | %s -> %s", topic, data);

            // Commande LED
            if (strcmp(topic, TOPIC_LED) == 0)
            {
                if      (strcmp(data, "ON")  == 0) set_led(true);
                else if (strcmp(data, "OFF") == 0) set_led(false);
                else ESP_LOGW(TAG, "Commande inconnue: %s", data);
                publish_status();
            }
        }
        break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Erreur MQTT");
            break;

        default:
            break;
    }
}

static void start_mqtt(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = BROKER_URI,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// ─── WiFi ─────────────────────────────────────────────────────────────────────

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WiFi perdu — reconnexion...");
        esp_wifi_connect();
    }
    else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
        char ip[16];
        esp_ip4addr_ntoa(&e->ip_info.ip, ip, sizeof(ip));
        ESP_LOGI(TAG, "IP: %s", ip);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = {};
    strlcpy((char *)wifi_cfg.sta.ssid,     WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, WIFI_PASS, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        false, true, portMAX_DELAY);
}

// ─── app_main ─────────────────────────────────────────────────────────────────

void app_main(void)
{
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // LED
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&cfg);
    gpio_set_level(LED_PIN, 0);

    // WiFi → attend connexion avant de continuer
    wifi_init();

    // MQTT
    start_mqtt();

    // Tâche publication température
    xTaskCreate(publish_task, "mqtt_pub", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "=== MQTT pret ===");
    ESP_LOGI(TAG, "Broker : %s", BROKER_URI);
    ESP_LOGI(TAG, "Sub    : %s", TOPIC_LED);
    ESP_LOGI(TAG, "Pub    : %s", TOPIC_STATUS);
    ESP_LOGI(TAG, "Pub    : %s", TOPIC_TEMP);
}
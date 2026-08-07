#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "mqtt_client.h"

static const char *TAG = "IOT";

// WiFi
#define WIFI_SSID           "Wokwi-GUEST"
#define WIFI_PASS           ""
#define WIFI_CONNECTED_BIT  BIT0

// MQTT
#define BROKER_URI    "mqtt://broker.hivemq.com:1883"
#define TOPIC_SENSORS "esp32-iot/sensors"
#define TOPIC_STATUS  "esp32-iot/status"

// GPIO
#define LED_PIN  GPIO_NUM_2
#define I2C_SDA  GPIO_NUM_21
#define I2C_SCL  GPIO_NUM_22

// MPU6050
#define MPU_ADDR        0x68
#define REG_PWR_MGMT_1  0x6B
#define REG_ACCEL_START 0x3B
#define ACCEL_SCALE     16384.0f
#define GYRO_SCALE      131.0f

// Structure données capteur

typedef struct {
    float ax, ay, az;
    float gx, gy, gz;
    float temp;
} sensor_data_t;

// Variables globales

static EventGroupHandle_t       wifi_event_group;
static SemaphoreHandle_t        mutex_state;
static QueueHandle_t            queue_sensors;
static esp_mqtt_client_handle_t mqtt_client;
static i2c_master_dev_handle_t  mpu_handle;

// État partagé entre les tâches (protégé par mutex)
static sensor_data_t g_data      = {0};
static bool          g_mqtt_ok   = false;
static uint32_t      g_pub_count = 0;

// MPU6050

static void mpu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    i2c_master_transmit(mpu_handle, buf, 2, 100);
}

static void mpu_read(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_master_transmit_receive(mpu_handle, &reg, 1, data, len, 100);
}

static int16_t to_int16(uint8_t hi, uint8_t lo)
{
    return (int16_t)((hi << 8) | lo);
}

static void mpu_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = I2C_NUM_0,
        .sda_io_num                   = I2C_SDA,
        .scl_io_num                   = I2C_SCL,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MPU_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &mpu_handle));

    mpu_write(REG_PWR_MGMT_1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "MPU6050 OK");
}

// Dashboard HTML

static const char HTML[] =
    "<!DOCTYPE html><html lang='fr'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 IoT</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:'Segoe UI',sans-serif;background:#0d0d1a;color:#e0e0e0;"
         "display:flex;flex-direction:column;align-items:center;padding:30px 16px}"
    "h1{color:#00d4ff;margin-bottom:22px;font-size:1.5em;letter-spacing:1px}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;"
          "width:100%;max-width:560px}"
    ".card{background:#1a1a2e;border:1px solid #2a2a4a;"
          "border-radius:14px;padding:18px}"
    ".card.full{grid-column:1/-1}"
    ".lbl{color:#555;font-size:.72em;text-transform:uppercase;"
         "letter-spacing:1px;margin-bottom:10px}"
    ".row{display:flex;justify-content:space-between;margin:5px 0}"
    ".row span{color:#888;font-size:.85em}"
    ".row strong{color:#00d4ff}"
    ".big{font-size:1.6em;font-weight:700;color:#00e676;margin:4px 0}"
    ".badge{display:inline-block;padding:4px 14px;border-radius:20px;"
           "font-size:.8em;font-weight:700;margin-bottom:8px}"
    ".on{background:#00e676;color:#000}.off{background:#ff1744;color:#fff}"
    ".sub{color:#555;font-size:.8em}"
    "</style></head><body>"
    "<h1>&#9889; ESP32 IoT Dashboard</h1>"
    "<div class='grid'>"
      "<div class='card'>"
        "<div class='lbl'>Accelerometre (g)</div>"
        "<div class='row'><span>X</span><strong id='ax'>--</strong></div>"
        "<div class='row'><span>Y</span><strong id='ay'>--</strong></div>"
        "<div class='row'><span>Z</span><strong id='az'>--</strong></div>"
      "</div>"
      "<div class='card'>"
        "<div class='lbl'>Gyroscope (deg/s)</div>"
        "<div class='row'><span>X</span><strong id='gx'>--</strong></div>"
        "<div class='row'><span>Y</span><strong id='gy'>--</strong></div>"
        "<div class='row'><span>Z</span><strong id='gz'>--</strong></div>"
      "</div>"
      "<div class='card'>"
        "<div class='lbl'>Temperature</div>"
        "<div class='big' id='tmp'>-- °C</div>"
      "</div>"
      "<div class='card'>"
        "<div class='lbl'>MQTT</div>"
        "<span class='badge off' id='mq'>--</span>"
        "<div class='sub' id='pub'>Publications : --</div>"
      "</div>"
    "</div>"
    "<script>"
    "function upd(){"
      "fetch('/api/data').then(r=>r.json()).then(d=>{"
        "document.getElementById('ax').textContent=d.ax.toFixed(2);"
        "document.getElementById('ay').textContent=d.ay.toFixed(2);"
        "document.getElementById('az').textContent=d.az.toFixed(2);"
        "document.getElementById('gx').textContent=d.gx.toFixed(1);"
        "document.getElementById('gy').textContent=d.gy.toFixed(1);"
        "document.getElementById('gz').textContent=d.gz.toFixed(1);"
        "document.getElementById('tmp').textContent=d.temp.toFixed(1)+' \u00b0C';"
        "let m=document.getElementById('mq');"
        "m.textContent=d.mqtt?'Connecte':'Deconnecte';"
        "m.className='badge '+(d.mqtt?'on':'off');"
        "document.getElementById('pub').textContent='Publications : '+d.pub;"
      "}).catch(()=>{})"
    "}"
    "setInterval(upd,1000);upd();"
    "</script></body></html>";

// HTTP Handlers

static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML, sizeof(HTML) - 1);
    return ESP_OK;
}

static esp_err_t h_data(httpd_req_t *req)
{
    xSemaphoreTake(mutex_state, portMAX_DELAY);
    sensor_data_t d   = g_data;
    bool          mq  = g_mqtt_ok;
    uint32_t      pub = g_pub_count;
    xSemaphoreGive(mutex_state);

    char json[256];
    snprintf(json, sizeof(json),
        "{\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,"
         "\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f,"
         "\"temp\":%.1f,\"mqtt\":%s,\"pub\":%lu}",
        d.ax, d.ay, d.az,
        d.gx, d.gy, d.gz,
        d.temp,
        mq ? "true" : "false",
        pub);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static void start_http(void)
{
    httpd_handle_t  server = NULL;
    httpd_config_t  cfg    = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    httpd_uri_t u0 = { .uri="/",         .method=HTTP_GET, .handler=h_root, .user_ctx=NULL };
    httpd_uri_t u1 = { .uri="/api/data", .method=HTTP_GET, .handler=h_data, .user_ctx=NULL };
    httpd_register_uri_handler(server, &u0);
    httpd_register_uri_handler(server, &u1);
    ESP_LOGI(TAG, "HTTP OK");
}

// MQTT

static void mqtt_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (id == MQTT_EVENT_CONNECTED)
    {
        xSemaphoreTake(mutex_state, portMAX_DELAY);
        g_mqtt_ok = true;
        xSemaphoreGive(mutex_state);
        esp_mqtt_client_publish(mqtt_client, TOPIC_STATUS, "online", 0, 1, 1);
        ESP_LOGI(TAG, "MQTT connecte");
    }
    else if (id == MQTT_EVENT_DISCONNECTED)
    {
        xSemaphoreTake(mutex_state, portMAX_DELAY);
        g_mqtt_ok = false;
        xSemaphoreGive(mutex_state);
        ESP_LOGW(TAG, "MQTT deconnecte");
    }
}

static void start_mqtt(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = BROKER_URI,
        .session.last_will  = {
            .topic  = TOPIC_STATUS,
            .msg    = "offline",
            .qos    = 1,
            .retain = 1,
        }
    };
    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// WiFi

static void wifi_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
        esp_wifi_connect();
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        char ip[16];
        esp_ip4addr_ntoa(&e->ip_info.ip, ip, sizeof(ip));
        ESP_LOGI(TAG, "IP: %s | Dashboard: http://%s", ip, ip);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_handler, NULL));

    wifi_config_t cfg = {};
    strlcpy((char *)cfg.sta.ssid,     WIFI_SSID, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, WIFI_PASS, sizeof(cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        false, true, portMAX_DELAY);
}

// TASKS

// Lit le MPU6050 toutes les 500ms
// Met à jour g_data (web server) + envoie dans la queue (MQTT)
static void task_sensor(void *arg)
{
    while (1)
    {
        uint8_t buf[14];
        mpu_read(REG_ACCEL_START, buf, 14);

        sensor_data_t d;
        d.ax   = to_int16(buf[0],  buf[1])  / ACCEL_SCALE;
        d.ay   = to_int16(buf[2],  buf[3])  / ACCEL_SCALE;
        d.az   = to_int16(buf[4],  buf[5])  / ACCEL_SCALE;
        d.temp = to_int16(buf[6],  buf[7])  / 340.0f + 36.53f;
        d.gx   = to_int16(buf[8],  buf[9])  / GYRO_SCALE;
        d.gy   = to_int16(buf[10], buf[11]) / GYRO_SCALE;
        d.gz   = to_int16(buf[12], buf[13]) / GYRO_SCALE;

        xSemaphoreTake(mutex_state, portMAX_DELAY);
        g_data = d;
        xSemaphoreGive(mutex_state);

        xQueueSend(queue_sensors, &d, 0);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Reçoit les données du capteur et publie sur MQTT
static void task_mqtt_pub(void *arg)
{
    sensor_data_t d;
    while (1)
    {
        if (xQueueReceive(queue_sensors, &d, portMAX_DELAY) != pdTRUE)
            continue;

        xSemaphoreTake(mutex_state, portMAX_DELAY);
        bool ok = g_mqtt_ok;
        xSemaphoreGive(mutex_state);

        if (!ok) continue;

        char msg[256];
        snprintf(msg, sizeof(msg),
            "{\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,"
             "\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f,"
             "\"temp\":%.1f}",
            d.ax, d.ay, d.az,
            d.gx, d.gy, d.gz,
            d.temp);

        esp_mqtt_client_publish(mqtt_client, TOPIC_SENSORS, msg, 0, 0, 0);

        xSemaphoreTake(mutex_state, portMAX_DELAY);
        g_pub_count++;
        uint32_t cnt = g_pub_count;
        xSemaphoreGive(mutex_state);

        if (cnt % 10 == 0)
            ESP_LOGI(TAG, "Pub #%lu → %s", cnt, TOPIC_SENSORS);
    }
}

// LED clignote = système en vie
static void task_status(void *arg)
{
    bool s = false;
    while (1)
    {
        s = !s;
        gpio_set_level(LED_PIN, s);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// app_main

void app_main(void)
{
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // LED status
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&led_cfg);

    // MPU6050 via I2C
    mpu_init();

    // Primitives FreeRTOS
    mutex_state   = xSemaphoreCreateMutex();
    queue_sensors = xQueueCreate(5, sizeof(sensor_data_t));

    // WiFi — bloquant jusqu'à connexion
    wifi_init();

    // Services réseau
    start_mqtt();
    start_http();

    // Tâches FreeRTOS
    xTaskCreate(task_sensor,   "sensor", 4096, NULL, 3, NULL);
    xTaskCreate(task_mqtt_pub, "mqttpub", 4096, NULL, 2, NULL);
    xTaskCreate(task_status,   "status", 1024, NULL, 1, NULL);

    ESP_LOGI(TAG, "=== Systeme IoT complet demarre ===");
    ESP_LOGI(TAG, "MQTT    : %s", BROKER_URI);
    ESP_LOGI(TAG, "Topics  : %s | %s", TOPIC_SENSORS, TOPIC_STATUS);
}
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

// WiFi — reseau simule Wokwi
#define WIFI_SSID           "Wokwi-GUEST"
#define WIFI_PASS           ""
#define WIFI_CONNECTED_BIT  BIT0

// GPIO
#define LED_RED_PIN    GPIO_NUM_2
#define LED_GREEN_PIN  GPIO_NUM_22
#define BTN_START_PIN  GPIO_NUM_4
#define BTN_SPEED_PIN  GPIO_NUM_5
#define MOTOR_PWM_PIN  GPIO_NUM_18

// LEDC
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_RESOLUTION LEDC_TIMER_8_BIT
#define PWM_FREQ_HZ     1000

static const char *TAG = "MOTOR_WEB";
static EventGroupHandle_t s_wifi_group;

static bool motor_on  = false;
static int  speed_idx = 0;
static const int speed_levels[] = {64, 128, 192, 255};

// Etat

static void apply_state(void)
{
    int duty = motor_on ? speed_levels[speed_idx] : 0;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    gpio_set_level(LED_GREEN_PIN, motor_on ? 1 : 0);
    gpio_set_level(LED_RED_PIN,   motor_on ? 0 : 1);
}

// Page HTML

static const char HTML[] =
    "<!DOCTYPE html><html lang='fr'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 Motor</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:'Segoe UI',sans-serif;background:#0d0d1a;color:#e0e0e0;"
         "display:flex;flex-direction:column;align-items:center;padding:40px 20px}"
    "h1{color:#00d4ff;font-size:1.7em;margin-bottom:28px;letter-spacing:1px}"
    ".card{background:#1a1a2e;border:1px solid #2a2a4a;border-radius:16px;"
          "padding:32px;width:100%;max-width:340px;text-align:center;"
          "box-shadow:0 8px 40px rgba(0,0,0,.6)}"
    ".lbl{color:#555;font-size:.75em;text-transform:uppercase;letter-spacing:1px;margin-bottom:8px}"
    ".st{font-size:3em;font-weight:700;margin-bottom:24px;transition:.3s}"
    ".st.on{color:#00e676} .st.off{color:#ff1744}"
    ".bars{display:flex;justify-content:center;gap:8px;margin:10px 0 6px}"
    ".bar{width:54px;height:54px;background:#111128;border:1px solid #2a2a4a;"
         "border-radius:8px;display:flex;align-items:center;justify-content:center;"
         "font-size:.75em;font-weight:700;color:#333;transition:.3s}"
    ".bar.a{background:#00e676;color:#000;border-color:#00e676}"
    ".sp{color:#00d4ff;font-size:1em;font-weight:700;margin-top:8px}"
    ".du{color:#444;font-size:.8em;margin-top:3px;margin-bottom:24px}"
    ".btns{display:flex;flex-direction:column;gap:10px}"
    "button{border:none;border-radius:10px;padding:16px;font-size:1em;"
           "font-weight:700;cursor:pointer;transition:.15s;letter-spacing:.5px}"
    "#bt{background:#ff1744;color:#fff}"
    "#bt.on{background:#00e676;color:#000}"
    "#bs{background:#1565c0;color:#fff}"
    "button:active{transform:scale(.97)}"
    "</style></head><body>"
    "<h1>&#9889; ESP32 Motor Control</h1>"
    "<div class='card'>"
      "<div class='lbl'>Etat moteur</div>"
      "<div class='st off' id='st'>OFF</div>"
      "<div class='lbl'>Vitesse</div>"
      "<div class='bars'>"
        "<div class='bar' id='b0'>25%</div>"
        "<div class='bar' id='b1'>50%</div>"
        "<div class='bar' id='b2'>75%</div>"
        "<div class='bar' id='b3'>100%</div>"
      "</div>"
      "<div class='sp' id='sp'>Niveau 1 / 4</div>"
      "<div class='du' id='du'>Duty : 64 / 255</div>"
      "<div class='btns'>"
        "<button id='bt' onclick='tog()'>START</button>"
        "<button id='bs' onclick='sp2()'>VITESSE +</button>"
      "</div>"
    "</div>"
    "<script>"
    "function upd(){"
      "fetch('/api/status').then(r=>r.json()).then(d=>{"
        "let s=document.getElementById('st');"
        "let b=document.getElementById('bt');"
        "s.textContent=d.m?'ON':'OFF';"
        "s.className='st '+(d.m?'on':'off');"
        "b.textContent=d.m?'STOP':'START';"
        "b.className=d.m?'on':'';"
        "document.getElementById('sp').textContent='Niveau '+(d.s+1)+' / 4';"
        "document.getElementById('du').textContent='Duty : '+d.d+' / 255';"
        "for(let i=0;i<4;i++){"
          "document.getElementById('b'+i).className='bar'+(i<=d.s?' a':'');"
        "}"
      "}).catch(()=>{})"
    "}"
    "function tog(){fetch('/api/toggle',{method:'POST'}).then(()=>upd())}"
    "function sp2(){fetch('/api/speed',{method:'POST'}).then(()=>upd())}"
    "setInterval(upd,1000);upd();"
    "</script></body></html>";

// Handlers HTTP

static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML, sizeof(HTML) - 1);
    return ESP_OK;
}

static esp_err_t h_status(httpd_req_t *req)
{
    char json[64];
    snprintf(json, sizeof(json),
        "{\"m\":%s,\"s\":%d,\"d\":%d}",
        motor_on ? "true" : "false",
        speed_idx,
        speed_levels[speed_idx]);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static esp_err_t h_toggle(httpd_req_t *req)
{
    motor_on = !motor_on;
    apply_state();
    ESP_LOGI(TAG, "WEB — Moteur: %s", motor_on ? "ON" : "OFF");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t h_speed(httpd_req_t *req)
{
    speed_idx = (speed_idx + 1) % 4;
    if (motor_on) apply_state();
    ESP_LOGI(TAG, "WEB — Vitesse: %d/4", speed_idx + 1);
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static void start_server(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t cfg    = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    httpd_uri_t u0 = { .uri="/",           .method=HTTP_GET,  .handler=h_root,   .user_ctx=NULL };
    httpd_uri_t u1 = { .uri="/api/status", .method=HTTP_GET,  .handler=h_status, .user_ctx=NULL };
    httpd_uri_t u2 = { .uri="/api/toggle", .method=HTTP_POST, .handler=h_toggle, .user_ctx=NULL };
    httpd_uri_t u3 = { .uri="/api/speed",  .method=HTTP_POST, .handler=h_speed,  .user_ctx=NULL };

    httpd_register_uri_handler(server, &u0);
    httpd_register_uri_handler(server, &u1);
    httpd_register_uri_handler(server, &u2);
    httpd_register_uri_handler(server, &u3);
}

// WiFi

static void wifi_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Reconnexion...");
        esp_wifi_connect();
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        char ip[16];
        esp_ip4addr_ntoa(&e->ip_info.ip, ip, sizeof(ip));
        ESP_LOGI(TAG, "IP: %s", ip);
        ESP_LOGI(TAG, ">>> Ouvre http://%s dans le navigateur <<<", ip);
        xEventGroupSetBits(s_wifi_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    s_wifi_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,       wifi_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP,    wifi_handler, NULL));

    wifi_config_t wifi_cfg = {};
    strlcpy((char *)wifi_cfg.sta.ssid,     WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, WIFI_PASS, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(s_wifi_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
}

// app_main

void app_main(void)
{
    // NVS (requis par WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // LEDs
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << LED_RED_PIN) | (1ULL << LED_GREEN_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&out_cfg);

    // Boutons
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BTN_START_PIN) | (1ULL << BTN_SPEED_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_cfg);

    // LEDC Timer
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz         = PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_cfg);

    // LEDC Channel
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = MOTOR_PWM_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0
    };
    ledc_channel_config(&ch_cfg);

    apply_state();

    wifi_init();
    start_server();

    // Boucle boutons physiques
    int last_s = 1, last_v = 1;

    while (1)
    {
        int s = gpio_get_level(BTN_START_PIN);
        int v = gpio_get_level(BTN_SPEED_PIN);

        if (last_s == 1 && s == 0)
        {
            motor_on = !motor_on;
            apply_state();
            ESP_LOGI(TAG, "BTN — Moteur: %s", motor_on ? "ON" : "OFF");
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if (last_v == 1 && v == 0)
        {
            speed_idx = (speed_idx + 1) % 4;
            if (motor_on) apply_state();
            ESP_LOGI(TAG, "BTN — Vitesse: %d/4", speed_idx + 1);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        last_s = s;
        last_v = v;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
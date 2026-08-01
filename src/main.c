#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG         = "BLE";
static const char *DEVICE_NAME = "ESP32-BLE";

#define LED_PIN  GPIO_NUM_2

static bool led_state = false;

// Forward declaration nécessaire car gap_event_cb est utilisé dans start_advertising
static int gap_event_cb(struct ble_gap_event *event, void *arg);

// GATT Callback

// Lecture : le client lit l'état actuel de la LED
static int chr_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *val = led_state ? "ON" : "OFF";
    os_mbuf_append(ctxt->om, val, strlen(val));
    return 0;
}

// Écriture : le client envoie "ON" ou "OFF"
static int chr_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    char buf[8] = {0};
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;

    ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    buf[len] = '\0';

    if (strcmp(buf, "ON") == 0)
    {
        led_state = true;
        gpio_set_level(LED_PIN, 1);
        ESP_LOGI(TAG, "LED ON");
    }
    else if (strcmp(buf, "OFF") == 0)
    {
        led_state = false;
        gpio_set_level(LED_PIN, 0);
        ESP_LOGI(TAG, "LED OFF");
    }
    else
    {
        ESP_LOGW(TAG, "Commande inconnue: %s", buf);
    }
    return 0;
}

// UUIDs 128 bits custom
// En production on génère des UUIDs réels (uuidgenerator.net)

static const ble_uuid128_t svc_uuid = BLE_UUID128_INIT(
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10
);

static const ble_uuid128_t chr_read_uuid = BLE_UUID128_INIT(
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
);

static const ble_uuid128_t chr_write_uuid = BLE_UUID128_INIT(
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30
);

// Table GATT
// Définit les services et caractéristiques exposés par l'ESP32

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // Lecture état LED
                .uuid      = &chr_read_uuid.u,
                .access_cb = chr_read_cb,
                .flags     = BLE_GATT_CHR_F_READ,
            },
            {
                // Écriture commande "ON" / "OFF"
                .uuid      = &chr_write_uuid.u,
                .access_cb = chr_write_cb,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 } // terminateur obligatoire
        }
    },
    { 0 } // terminateur obligatoire
};

// Advertising

static void start_advertising(void)
{
    // Payload advertisement : nom du device + flags
    struct ble_hs_adv_fields fields = {0};
    fields.flags            = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name             = (uint8_t *)DEVICE_NAME;
    fields.name_len         = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    // Paramètres : connectable + discoverable
    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // undirected connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  // general discoverable

    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &adv_params, gap_event_cb, NULL);

    ESP_LOGI(TAG, "Advertising — cherche '%s' dans nRF Connect", DEVICE_NAME);
}

// GAP Events

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0)
                ESP_LOGI(TAG, "Client connecte — handle %d",
                         event->connect.conn_handle);
            else
                start_advertising(); // echec → relance
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Client deconnecte (raison: %d) — relance advertising",
                     event->disconnect.reason);
            start_advertising();
            break;

        default:
            break;
    }
    return 0;
}

// NimBLE init

// Appelé quand le stack BLE est synchronisé et prêt
static void ble_on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    start_advertising();
}

// Task FreeRTOS dédiée au stack NimBLE
static void ble_host_task(void *param)
{
    nimble_port_run();              // bloquant — tourne indéfiniment
    nimble_port_freertos_deinit();
}

// app_main

void app_main(void)
{
    // NVS requis par NimBLE pour stocker les clés de pairing
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

    // Init NimBLE
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(DEVICE_NAME);

    // Enregistrement des services GATT
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    // Callback déclenché quand le stack est prêt
    ble_hs_cfg.sync_cb = ble_on_sync;

    // Démarre la task BLE sur FreeRTOS
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "=== BLE pret ===");
    ESP_LOGI(TAG, "Ouvre nRF Connect → scan → connecte a '%s'", DEVICE_NAME);
    ESP_LOGI(TAG, "Caracteristique WRITE → envoie 'ON' ou 'OFF'");
}
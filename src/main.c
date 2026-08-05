#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "soc/gpio_reg.h"   // adresses registres GPIO officielles ESP-IDF

static const char *TAG = "REG";

// Adresses des registres GPIO
// Ces adresses viennent du Technical Reference Manual ESP32 (p.73)
// Elles sont aussi définies dans soc/gpio_reg.h — on utilise les constantes

// GPIO_OUT_W1TS_REG → Write-1-to-Set  : bit N à 1 = GPIO N HIGH (atomique)
// GPIO_OUT_W1TC_REG → Write-1-to-Clear: bit N à 1 = GPIO N LOW  (atomique)
// GPIO_IN_REG       → lecture état actuel de toutes les entrées

// REG_READ et REG_WRITE sont déjà définis dans soc/soc.h (inclus via esp-idf)
// On les utilise directement sans les redéfinir

// Macros GPIO lisibles — utilisent REG_READ/REG_WRITE d'ESP-IDF
#define MY_GPIO_SET(pin)  REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << (pin)))
#define MY_GPIO_CLR(pin)  REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << (pin)))
#define MY_GPIO_GET(pin)  ((REG_READ(GPIO_IN_REG) >> (pin)) & 0x1)

#define LED_PIN  2
#define BTN_PIN  4

// Benchmark
static void benchmark(void)
{
    const int N = 100000;

    // Méthode ESP-IDF
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < N; i++)
    {
        gpio_set_level(LED_PIN, 1);
        gpio_set_level(LED_PIN, 0);
    }
    int64_t t_api = esp_timer_get_time() - t0;

    // Méthode registre direct
    t0 = esp_timer_get_time();
    for (int i = 0; i < N; i++)
    {
        MY_GPIO_SET(LED_PIN);
        MY_GPIO_CLR(LED_PIN);
    }
    int64_t t_reg = esp_timer_get_time() - t0;

    ESP_LOGI(TAG, "=== BENCHMARK (%d iterations) ===", N);
    ESP_LOGI(TAG, "ESP-IDF gpio_set_level : %lld us", t_api);
    ESP_LOGI(TAG, "Registre direct        : %lld us", t_reg);
    ESP_LOGI(TAG, "Gain                   : x%.1f plus rapide",
        (float)t_api / (float)t_reg);
}

void app_main(void)
{
    // Init via ESP-IDF — obligatoire pour configurer le mux, pull-up, mode
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&led_cfg);

    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BTN_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_cfg);

    // Affichage valeurs registres au démarrage
    ESP_LOGI(TAG, "=== Registres GPIO ESP32 ===");
    ESP_LOGI(TAG, "GPIO_OUT_REG    @ 0x%08X = 0x%08lX",
        GPIO_OUT_REG,    REG_READ(GPIO_OUT_REG));
    ESP_LOGI(TAG, "GPIO_ENABLE_REG @ 0x%08X = 0x%08lX",
        GPIO_ENABLE_REG, REG_READ(GPIO_ENABLE_REG));
    ESP_LOGI(TAG, "GPIO_IN_REG     @ 0x%08X = 0x%08lX",
        GPIO_IN_REG,     REG_READ(GPIO_IN_REG));

    benchmark();

    ESP_LOGI(TAG, "=== Boucle principale ===");
    ESP_LOGI(TAG, "BTN GPIO%d → toggle LED GPIO%d via registre direct", BTN_PIN, LED_PIN);

    bool led_on  = false;
    int  last_btn = 1;

    while (1)
    {
        int btn = MY_GPIO_GET(BTN_PIN);

        if (last_btn == 1 && btn == 0)
        {
            led_on = !led_on;

            if (led_on)
                MY_GPIO_SET(LED_PIN);
            else
                MY_GPIO_CLR(LED_PIN);

            uint32_t out = REG_READ(GPIO_OUT_REG);
            ESP_LOGI(TAG, "LED %s | GPIO_OUT_REG = 0x%08lX | bit%d = %lu",
                led_on ? "ON " : "OFF",
                out,
                LED_PIN,
                (out >> LED_PIN) & 0x1);
        }

        last_btn = btn;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
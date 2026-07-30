#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "ADC";

#define POT_CHANNEL     ADC_CHANNEL_6
#define ADC_MAX         4095
#define SAMPLES         8       // lectures moyennées pour réduire le bruit
#define CHANGE_THRESH   20      // seuil minimum pour déclencher une mise à jour

#define LED_PIN         GPIO_NUM_18
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_CHAN       LEDC_CHANNEL_0
#define LEDC_RES        LEDC_TIMER_8_BIT
#define PWM_FREQ        1000

// Moyenne de SAMPLES lectures pour lisser le bruit ADC
static int read_avg(adc_oneshot_unit_handle_t handle)
{
    int sum = 0;
    for (int i = 0; i < SAMPLES; i++)
    {
        int val = 0;
        adc_oneshot_read(handle, POT_CHANNEL, &val);
        sum += val;
    }
    return sum / SAMPLES;
}

void app_main(void)
{
    // --- ADC ---
    adc_oneshot_unit_handle_t adc_handle;

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, POT_CHANNEL, &chan_cfg));

    // --- LEDC Timer ---
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RES,
        .freq_hz         = PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_cfg);

    // --- LEDC Channel ---
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = LED_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHAN,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE
    };
    ledc_channel_config(&ch_cfg);

    ESP_LOGI(TAG, "=== ADC pret — tourne le potentiometre ===");

    int last_raw = -1;

    while (1)
    {
        int raw  = read_avg(adc_handle);
        int duty = (raw * 255) / ADC_MAX;

        // Mise à jour uniquement si changement > seuil
        if (abs(raw - last_raw) > CHANGE_THRESH)
        {
            ledc_set_duty(LEDC_MODE, LEDC_CHAN, duty);
            ledc_update_duty(LEDC_MODE, LEDC_CHAN);

            ESP_LOGI(TAG, "raw: %4d | duty: %3d/255 | %3d%%",
                raw, duty, (duty * 100) / 255);

            last_raw = raw;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
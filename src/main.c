#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "MOTOR";

#define LED_RED_PIN     GPIO_NUM_2
#define LED_GREEN_PIN   GPIO_NUM_22   
#define BTN_START_PIN   GPIO_NUM_4
#define BTN_SPEED_PIN   GPIO_NUM_5
#define MOTOR_PWM_PIN   GPIO_NUM_18

#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_RESOLUTION LEDC_TIMER_8_BIT
#define PWM_FREQ_HZ     1000

static bool motor_on = false;
static int  speed_idx = 0;
static const int speed_levels[] = {64, 128, 192, 255};

static void apply_state(void)
{
    int duty = motor_on ? speed_levels[speed_idx] : 0;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

    gpio_set_level(LED_GREEN_PIN, motor_on ? 1 : 0);  // VERTE = moteur ON
    gpio_set_level(LED_RED_PIN,   motor_on ? 0 : 1);  // ROUGE = moteur OFF
}

void app_main(void)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << LED_RED_PIN) | (1ULL << LED_GREEN_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&out_cfg);

    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BTN_START_PIN) | (1ULL << BTN_SPEED_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_cfg);

    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz         = PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_cfg);

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

    ESP_LOGI(TAG, "=== Systeme pret === LED ROUGE = arret | LED VERTE = marche");
    ESP_LOGI(TAG, "D4=START/STOP | D5=VITESSE | LED bleue=moteur");

    int last_start = 1;
    int last_speed = 1;

    while (1)
    {
        int start_btn = gpio_get_level(BTN_START_PIN);
        int speed_btn = gpio_get_level(BTN_SPEED_PIN);

        // --- Bouton START ---
        if (last_start == 1 && start_btn == 0)
        {
            motor_on = !motor_on;
            apply_state();
            ESP_LOGI(TAG, "Moteur: %s | Vitesse: %d/4 | Duty: %d/255",
                motor_on ? "ON " : "OFF",
                speed_idx + 1,
                motor_on ? speed_levels[speed_idx] : 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        // --- Bouton SPEED ---
        if (last_speed == 1 && speed_btn == 0)
        {
            speed_idx = (speed_idx + 1) % 4;

            // Flash LED bleue meme si moteur OFF → confirmation visuelle
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, speed_levels[speed_idx]);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            vTaskDelay(pdMS_TO_TICKS(150));
            apply_state(); // remet l'etat reel

            ESP_LOGI(TAG, "Vitesse: %d/4 | Duty: %d/255 | %d%%",
                speed_idx + 1,
                speed_levels[speed_idx],
                (speed_levels[speed_idx] * 100) / 255);
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        last_start = start_btn;
        last_speed = speed_btn;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

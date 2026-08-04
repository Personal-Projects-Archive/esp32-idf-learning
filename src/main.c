#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "FREERTOS";

// GPIO
#define POT_CHANNEL    ADC_CHANNEL_6  // GPIO34 potentiometre
#define LED_PWM_PIN    GPIO_NUM_18    // LED bleue controlée par PWM
#define LED_STATUS_PIN GPIO_NUM_2     // LED verte — clignote = systeme vivant
#define BTN_PIN        GPIO_NUM_4     // bouton toggle

// LEDC
#define LEDC_MODE  LEDC_LOW_SPEED_MODE
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_CHAN  LEDC_CHANNEL_0
#define LEDC_RES   LEDC_TIMER_8_BIT
#define PWM_FREQ   1000

// Event bit
#define EVT_BTN_PRESSED  BIT0

// Primitives FreeRTOS
static QueueHandle_t      queue_adc;    // Canal ADC → LED
static SemaphoreHandle_t  mutex_stats;  // Protège les compteurs partagés
static EventGroupHandle_t event_group;  // Signaux bouton → LED

// Variables partagées (protégées par mutex_stats)
static uint32_t adc_read_count = 0;  // Nb lectures ADC
static uint32_t led_upd_count  = 0;  // Nb mises à jour PWM
static bool     led_enabled    = true;

// LEDC

static void ledc_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RES,
        .freq_hz         = PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num   = LED_PWM_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHAN,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE
    };
    ledc_channel_config(&ch);
}

static void set_duty(int duty)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHAN, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHAN);
}

// TASK 1 : ADC (Priorité 3 — HAUTE)
// Lit le potentiomètre toutes les 50ms
// Envoie le duty dans la queue si changement > 10 points
// Incrémente adc_read_count (protégé par mutex)

static void task_adc(void *arg)
{
    adc_oneshot_unit_handle_t adc;

    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&unit_cfg, &adc);

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12
    };
    adc_oneshot_config_channel(adc, POT_CHANNEL, &ch_cfg);

    int last = -1;

    while (1)
    {
        int raw = 0;
        adc_oneshot_read(adc, POT_CHANNEL, &raw);
        int duty = (raw * 255) / 4095;

        if (abs(raw - last) > 10)
        {
            // Envoie dans la queue — non-bloquant (timeout 0)
            // Si la queue est pleine, on abandonne ce cycle
            xQueueSend(queue_adc, &duty, 0);
            last = raw;

            // Mutex : protège adc_read_count partagé avec task_monitor
            xSemaphoreTake(mutex_stats, portMAX_DELAY);
            adc_read_count++;
            xSemaphoreGive(mutex_stats);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// TASK 2 : BOUTON (Priorité 3 — HAUTE)
// Détecte le front descendant
// Signale via EventGroup → task_led réagit

static void task_button(void *arg)
{
    int last = 1;

    while (1)
    {
        int state = gpio_get_level(BTN_PIN);

        // Front descendant → set bit dans l'event group
        // task_led attendra ce bit pour toggle la LED
        if (last == 1 && state == 0)
        {
            xEventGroupSetBits(event_group, EVT_BTN_PRESSED);
            ESP_LOGI(TAG, "[button] Bouton presse");
        }

        last = state;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// TASK 3 : LED (Priorité 2 — MOYENNE)
// Reçoit le duty depuis la queue ADC
// Vérifie l'event group pour le toggle bouton
// Met à jour le PWM si LED activée

static void task_led(void *arg)
{
    while (1)
    {
        // Vérifie l'event group — non-bloquant (timeout 0)
        // pdTRUE = efface le bit après lecture
        EventBits_t bits = xEventGroupWaitBits(
            event_group,
            EVT_BTN_PRESSED,
            pdTRUE,   // clear after read
            pdFALSE,  // n'attend pas TOUS les bits
            0         // timeout 0 = non-bloquant
        );

        if (bits & EVT_BTN_PRESSED)
        {
            // Toggle led_enabled (protégé par mutex)
            xSemaphoreTake(mutex_stats, portMAX_DELAY);
            led_enabled = !led_enabled;
            bool state  = led_enabled;
            xSemaphoreGive(mutex_stats);

            ESP_LOGI(TAG, "[led] LED %s", state ? "activee" : "desactivee");
            if (!state) set_duty(0);
        }

        // Attend une valeur ADC — bloque 100ms max
        int duty = 0;
        if (xQueueReceive(queue_adc, &duty, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            bool enabled;
            xSemaphoreTake(mutex_stats, portMAX_DELAY);
            enabled = led_enabled;
            xSemaphoreGive(mutex_stats);

            if (enabled)
            {
                set_duty(duty);

                // Mutex : protège led_upd_count partagé avec task_monitor
                xSemaphoreTake(mutex_stats, portMAX_DELAY);
                led_upd_count++;
                xSemaphoreGive(mutex_stats);

                ESP_LOGI(TAG, "[led] duty: %d/255 | %d%%",
                    duty, (duty * 100) / 255);
            }
        }
    }
}

// TASK 4 : MONITOR (Priorité 1 — BASSE) 
// Affiche des stats système toutes les 5 secondes
// Montre le nombre de tâches actives + compteurs partagés

static void task_monitor(void *arg)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));

        // Lit les compteurs partagés via mutex
        xSemaphoreTake(mutex_stats, portMAX_DELAY);
        uint32_t adc_cnt = adc_read_count;
        uint32_t led_cnt = led_upd_count;
        bool     enabled = led_enabled;
        xSemaphoreGive(mutex_stats);

        UBaseType_t nb_tasks = uxTaskGetNumberOfTasks();
        UBaseType_t stack    = uxTaskGetStackHighWaterMark(NULL);

        ESP_LOGI(TAG, "=== MONITOR ===");
        ESP_LOGI(TAG, "  Tasks actives  : %u", (unsigned)nb_tasks);
        ESP_LOGI(TAG, "  Stack libre    : %u words", (unsigned)stack);
        ESP_LOGI(TAG, "  Lectures ADC   : %lu", adc_cnt);
        ESP_LOGI(TAG, "  Updates PWM    : %lu", led_cnt);
        ESP_LOGI(TAG, "  LED            : %s", enabled ? "ON" : "OFF");
    }
}

// TASK 5 : BLINK (Priorité 1 — BASSE)
// Fait clignoter la LED verte toutes les 500ms
// Preuve visuelle que le scheduler FreeRTOS tourne

static void task_blink(void *arg)
{
    bool state = false;
    while (1)
    {
        state = !state;
        gpio_set_level(LED_STATUS_PIN, state);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// app_main

void app_main(void)
{
    // GPIO
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << LED_STATUS_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&out_cfg);

    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BTN_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_cfg);

    ledc_init();

    // Création des primitives FreeRTOS
    queue_adc   = xQueueCreate(10, sizeof(int));   // file de 10 entiers
    mutex_stats = xSemaphoreCreateMutex();          // protège les compteurs
    event_group = xEventGroupCreate();              // signal bouton → LED

    // Création des tâches
    // xTaskCreate(fonction, nom, stack(words), arg, priorité, handle)
    // Priorité : plus le chiffre est élevé, plus la tâche est prioritaire
    xTaskCreate(task_adc,     "adc",     2048, NULL, 3, NULL);
    xTaskCreate(task_button,  "button",  1024, NULL, 3, NULL);
    xTaskCreate(task_led,     "led",     2048, NULL, 2, NULL);
    xTaskCreate(task_monitor, "monitor", 2048, NULL, 1, NULL);
    xTaskCreate(task_blink,   "blink",   1024, NULL, 1, NULL);

    ESP_LOGI(TAG, "=== FreeRTOS avance — 5 taches actives ===");
    ESP_LOGI(TAG, "Pot GPIO34 → PWM LED bleue GPIO18");
    ESP_LOGI(TAG, "BTN GPIO4  → toggle LED | LED verte GPIO2 = systeme vivant");
    ESP_LOGI(TAG, "Stats toutes les 5 secondes dans le terminal");
}
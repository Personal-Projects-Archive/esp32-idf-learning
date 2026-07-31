#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "MPU6050";

// --- Broches I2C ---
// I2C = protocole 2 fils : SDA (données) + SCL (horloge)
#define I2C_SDA         GPIO_NUM_21
#define I2C_SCL         GPIO_NUM_22
#define I2C_FREQ_HZ     400000   // 400kHz = mode "fast" I2C

// --- MPU6050 ---
// Adresse I2C du capteur (broche AD0 à GND = 0x68, AD0 à VCC = 0x69)
#define MPU_ADDR        0x68
#define REG_PWR_MGMT_1  0x6B   // registre gestion alimentation — bit0=sleep
#define REG_ACCEL_START 0x3B   // premier registre de données (14 bytes contigus)

// Facteurs de conversion raw → unités physiques (config sortie usine)
// ±2g    → 16384 LSB par g
// ±250°/s → 131 LSB par °/s
#define ACCEL_SCALE     16384.0f
#define GYRO_SCALE      131.0f

// Seuils minimum pour déclencher un log (évite le spam si capteur stable)
#define THRESH_ACCEL    0.05f   // en g
#define THRESH_GYRO     2.0f    // en °/s
#define THRESH_TEMP     0.5f    // en °C

static i2c_master_dev_handle_t mpu_handle;

// Écrire un byte dans un registre du MPU6050
// Protocole : [adresse capteur] [registre] [valeur]
static void mpu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    ESP_ERROR_CHECK(i2c_master_transmit(mpu_handle, buf, 2, 100));
}

// Lire N bytes consécutifs depuis un registre
// Protocole : envoie le registre de départ, puis lit N bytes en réponse
static void mpu_read(uint8_t reg, uint8_t *data, size_t len)
{
    ESP_ERROR_CHECK(i2c_master_transmit_receive(mpu_handle, &reg, 1, data, len, 100));
}

// Assembler 2 bytes en entier signé 16 bits
// Le MPU envoie les données en big-endian : octet fort d'abord
static int16_t to_int16(uint8_t hi, uint8_t lo)
{
    return (int16_t)((hi << 8) | lo);
}

// Vérifie si la différence entre deux floats dépasse un seuil
static bool changed_f(float a, float b, float thresh)
{
    return fabsf(a - b) > thresh;
}

void app_main(void)
{
    // --- Initialisation du bus I2C ---
    // Un bus peut avoir plusieurs devices (MPU6050, OLED, etc.)
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = I2C_NUM_0,       // port I2C 0
        .sda_io_num                   = I2C_SDA,
        .scl_io_num                   = I2C_SCL,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,               // filtre bruit sur le bus
        .flags.enable_internal_pullup = true,            // résistances pull-up internes
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    // --- Ajout du MPU6050 sur le bus ---
    // Chaque device a sa propre adresse 7 bits
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MPU_ADDR,       // 0x68
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &mpu_handle));

    // Sortie du mode sleep — par défaut le MPU6050 démarre en veille
    // Écrire 0x00 dans PWR_MGMT_1 = réveil complet
    mpu_write(REG_PWR_MGMT_1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));   // attente stabilisation

    ESP_LOGI(TAG, "=== MPU6050 pret ===");

    // Initialisés hors plage pour forcer l'affichage au premier cycle
    float last_ax = 999, last_ay = 999, last_az = 999;
    float last_gx = 999, last_gy = 999, last_gz = 999;
    float last_temp = 999;

    while (1)
    {
        uint8_t buf[14];

        // Lecture burst : 14 bytes depuis 0x3B en une seule transaction I2C
        // Structure des 14 bytes :
        // [0-1]  Accel X  [2-3]  Accel Y  [4-5]  Accel Z
        // [6-7]  Temp
        // [8-9]  Gyro X   [10-11] Gyro Y  [12-13] Gyro Z
        mpu_read(REG_ACCEL_START, buf, 14);

        // Conversion raw int16 → unités physiques
        float ax   = to_int16(buf[0],  buf[1])  / ACCEL_SCALE;   // g
        float ay   = to_int16(buf[2],  buf[3])  / ACCEL_SCALE;   // g
        float az   = to_int16(buf[4],  buf[5])  / ACCEL_SCALE;   // g
        float temp = to_int16(buf[6],  buf[7])  / 340.0f + 36.53f; // °C (formule datasheet)
        float gx   = to_int16(buf[8],  buf[9])  / GYRO_SCALE;    // °/s
        float gy   = to_int16(buf[10], buf[11]) / GYRO_SCALE;    // °/s
        float gz   = to_int16(buf[12], buf[13]) / GYRO_SCALE;    // °/s

        // Log uniquement si au moins un axe a changé au-delà du seuil
        bool accel_changed = changed_f(ax, last_ax, THRESH_ACCEL)
                          || changed_f(ay, last_ay, THRESH_ACCEL)
                          || changed_f(az, last_az, THRESH_ACCEL);

        bool gyro_changed  = changed_f(gx, last_gx, THRESH_GYRO)
                          || changed_f(gy, last_gy, THRESH_GYRO)
                          || changed_f(gz, last_gz, THRESH_GYRO);

        bool temp_changed  = changed_f(temp, last_temp, THRESH_TEMP);

        if (accel_changed || gyro_changed || temp_changed)
        {
            ESP_LOGI(TAG, "Accel  X:%6.2f  Y:%6.2f  Z:%6.2f  g",     ax, ay, az);
            ESP_LOGI(TAG, "Gyro   X:%6.2f  Y:%6.2f  Z:%6.2f  deg/s", gx, gy, gz);
            ESP_LOGI(TAG, "Temp   %.1f C", temp);
            ESP_LOGI(TAG, "---");

            // Mise à jour des valeurs précédentes
            last_ax = ax; last_ay = ay; last_az = az;
            last_gx = gx; last_gy = gy; last_gz = gz;
            last_temp = temp;
        }

        vTaskDelay(pdMS_TO_TICKS(100));   // lecture toutes les 100ms
    }
}
# ESP32 — Apprentissage ESP-IDF

Parcours d'apprentissage complet de l'ESP32 avec le framework **ESP-IDF**.  
Chaque commit dans l'historique correspond à un projet ou exercice indépendant.

Environnement : **VSCode + PlatformIO + Wokwi Simulator**

---

## Projets

| # | Dossier | Concept principal |
|---|---|---|
| 01 | `01-pwm-motor-control` | Système moteur — boutons + LEDs d'état + PWM |
| 02 | `02-web-server` | Serveur HTTP embarqué — WiFi, REST API, HTML en C |
| 03 | `03-adc-potentiometer` | ADC — lecture analogique, mapping vers PWM |
| 04 | `04-i2c-mpu6050` | I2C — accéléromètre + gyroscope MPU6050 |
| 05 | `05-ble-nimble` | BLE — serveur GATT, advertising, NimBLE |
| 06 | `06-mqtt` | MQTT — publish/subscribe, broker HiveMQ |
| 07 | `07-freertos-advanced` | FreeRTOS — tasks, queue, mutex, event group |
| 08 | `08-direct-registers` | Registres directs — GPIO sans API, accès mémoire |
| 09 | `09-iot-complete` | **Projet final** — MPU6050 + MQTT + Web Server + FreeRTOS |

---

## Stack technique

| Couche | Technologie |
|---|---|
| Firmware | ESP-IDF 6.x |
| Build | PlatformIO + CMake |
| Simulation | Wokwi for VSCode |
| Périphériques | GPIO · PWM (LEDC) · ADC · I2C · UART |
| Connectivité | WiFi · BLE (NimBLE) · MQTT · HTTP Server |
| RTOS | FreeRTOS — tasks, queues, mutex, event groups |
| Bas niveau | Registres directs via `soc/gpio_reg.h` |

---

## Progression des concepts

```
GPIO → PWM → ADC → I2C
              │
              ▼
         FreeRTOS (tasks, queues, mutex)
              │
              ▼
    WiFi → HTTP Server → MQTT → BLE
              │
              ▼
         Registres directs
              │
              ▼
        Projet IoT complet
```

---

## Environnement

- **OS :** Windows
- **IDE :** VSCode + extension PlatformIO
- **Simulateur :** Wokwi for VSCode
- **Framework :** ESP-IDF via PlatformIO (`framework = espidf`)
- **Board :** ESP32 DevKit C V4
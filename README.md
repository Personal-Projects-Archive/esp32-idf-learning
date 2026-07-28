# ESP32 Web Server — Contrôle moteur via interface web

Serveur HTTP embarqué sur ESP32. Une page web hébergée directement sur le microcontrôleur permet de contrôler un moteur DC simulé en temps réel, sans application externe.

## Fonctionnement

- **Bouton Start/Stop** — démarre ou arrête le moteur (LED verte = ON, rouge = OFF)
- **Bouton Speed** — cycle entre 4 niveaux de vitesse PWM (25% → 50% → 75% → 100%)
- **Page web** — reflète l'état en temps réel (fetch toutes les secondes)
- **Boutons physiques** — fonctionnent en parallèle de l'interface web

## Stack

| Couche | Technologie |
|---|---|
| Firmware | ESP-IDF |
| Build | PlatformIO + CMake |
| WiFi | `esp_wifi` en mode STA |
| Serveur HTTP | `esp_http_server` natif ESP-IDF |
| PWM | LEDC — 4 niveaux, 8 bits, 1kHz |
| Frontend | HTML/CSS/JS embarqué en C, fetch API |
| Simulation | Wokwi for VSCode |

## Schéma GPIO

| GPIO | Rôle | Composant |
|---|---|---|
| D2 | LED rouge | Moteur OFF |
| D22 | LED verte | Moteur ON |
| D18 | PWM | Intensité = vitesse moteur |
| D4 | Bouton START | Pull-up interne |
| D5 | Bouton SPEED | Pull-up interne |

## API REST embarquée

| Endpoint | Méthode | Action |
|---|---|---|
| `/` | GET | Sert la page HTML |
| `/api/status` | GET | Retourne JSON `{motor, speed, duty}` |
| `/api/toggle` | POST | Toggle ON/OFF |
| `/api/speed` | POST | Vitesse suivante |

## Test sur hardware réel

Changer le SSID/password dans `main.c` :

```c
#define WIFI_SSID  "TonSSID"
#define WIFI_PASS  "TonMotDePasse"
```

Flasher l'ESP32, ouvrir le terminal série → l'IP s'affiche au démarrage.
Ouvrir `http://<IP>` dans le navigateur.

## Concepts appris

`esp_wifi` · `esp_netif` · `esp_http_server` · `httpd_uri_t` · HTML embarqué en C · JSON API · NVS Flash · `nvs_flash_init` · architecture event-driven WiFi
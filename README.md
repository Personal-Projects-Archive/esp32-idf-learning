# PWM Motor Control — Boutons + LEDs d'état

Deux boutons contrôlent un moteur simulé par une LED bleue (PWM).

- **BTN D4** — Start / Stop moteur
- **BTN D5** — Change la vitesse (4 niveaux : 25% → 50% → 75% → 100%)
- **LED Rouge D2** — Moteur à l'arrêt
- **LED Verte D22** — Moteur en marche
- **LED Bleue D18** — Intensité = vitesse PWM

Concepts : `gpio_config`, pull-up interne, front descendant, `ledc_timer_config`, `ledc_set_duty`, `ESP_LOGI`
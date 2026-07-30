# ADC — Potentiomètre → LED PWM

Lecture analogique via ADC1 (GPIO34), convertie en duty cycle PWM pour contrôler la luminosité d'une LED bleue (GPIO18).

Tourne le potentiomètre → intensité de la LED change en temps réel.
Le terminal affiche uniquement quand la valeur change (seuil de 20 points).

## Ce que ça fait

- Lecture ADC moyennée sur 8 échantillons pour réduire le bruit
- Mapping 0–4095 → 0–255 (duty PWM 8 bits)
- Mise à jour LED + log uniquement si changement significatif
- Fréquence PWM : 1kHz

## GPIO

| GPIO | Rôle |
|---|---|
| D34 | Lecture potentiomètre (ADC1 canal 6) |
| D18 | LED bleue PWM |

## Concepts

`adc_oneshot_new_unit` · `adc_oneshot_config_channel` · `adc_oneshot_read` · `ADC_ATTEN_DB_12` · `ADC_BITWIDTH_12` · moyennage · mapping ADC → PWM
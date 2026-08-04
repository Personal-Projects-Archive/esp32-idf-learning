# FreeRTOS Avancé — 5 tâches en parallèle

Démonstration des primitives FreeRTOS sur ESP32 :
5 tâches tournent en parallèle et communiquent entre elles via Queue, Mutex et Event Group.

## Ce que ça fait concrètement

Tourne le potentiomètre → la LED bleue change d'intensité en temps réel.
Appuie sur le bouton → la LED bleue s'éteint / se rallume (toggle).
La LED verte clignote en permanence → preuve que le scheduler tourne.
Toutes les 5 secondes → le terminal affiche les stats système.

## Les 5 tâches

| Tâche | Priorité | Rôle |
|---|---|---|
| `task_adc` | 3 (haute) | Lit le potentiomètre toutes les 50ms, envoie le duty dans la Queue |
| `task_button` | 3 (haute) | Détecte l'appui bouton, envoie un signal via Event Group |
| `task_led` | 2 (moyenne) | Reçoit le duty (Queue) et le signal bouton (Event Group), met à jour le PWM |
| `task_monitor` | 1 (basse) | Affiche les stats toutes les 5s (nb tâches, stack libre, compteurs) |
| `task_blink` | 1 (basse) | Fait clignoter la LED verte toutes les 500ms |

## Pourquoi 3 primitives différentes ?

**Queue** → transporter des données d'une tâche à l'autre (valeur ADC → duty PWM).
Fonctionne comme un tuyau : task_adc pousse, task_led reçoit.

**Mutex** → protéger une variable partagée par plusieurs tâches.
Sans mutex, deux tâches pourraient lire/écrire en même temps → corruption de données.

**Event Group** → envoyer un signal sans données (juste "ça s'est passé").
Le bouton lève un bit, task_led le détecte et toggle la LED.

## GPIO

| GPIO | Rôle |
|---|---|
| D34 | Potentiomètre (ADC1 canal 6) |
| D18 | LED bleue PWM (intensité = vitesse) |
| D2 | LED verte status (clignote = système vivant) |
| D4 | Bouton toggle LED bleue |

## Concepts

`xTaskCreate` · priorités tâches · `xQueueCreate` · `xQueueSend` · `xQueueReceive` ·
`xSemaphoreCreateMutex` · `xSemaphoreTake` · `xSemaphoreGive` ·
`xEventGroupCreate` · `xEventGroupSetBits` · `xEventGroupWaitBits` ·
`uxTaskGetNumberOfTasks` · `uxTaskGetStackHighWaterMark`
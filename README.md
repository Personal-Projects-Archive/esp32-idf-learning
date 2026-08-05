# Registres Directs — GPIO sans API

Piloter les GPIO en écrivant directement dans la mémoire de l'ESP32,
sans passer par les fonctions ESP-IDF comme `gpio_set_level()`.

## Ce que ça fait

- Affiche les valeurs des registres GPIO au démarrage
- Compare la vitesse ESP-IDF vs accès direct sur 100 000 opérations
- Bouton toggle LED entièrement via registres directs
- Affiche le registre `GPIO_OUT_REG` après chaque toggle — on voit le bit changer en direct

## Ce qu'on voit dans le terminal

```
GPIO_OUT_REG    @ 0x3FF44004 = 0x00000000
GPIO_ENABLE_REG @ 0x3FF44020 = 0x00000004

ESP-IDF gpio_set_level : ~300000 us
Registre direct        :  ~48000 us

BTN GPIO4 → toggle LED GPIO2 via registre direct
LED ON  | GPIO_OUT_REG = 0x00000004 | bit2 = 1
LED OFF | GPIO_OUT_REG = 0x00000000 | bit2 = 0
```

## Pourquoi c'est plus rapide

`gpio_set_level()` fait des vérifications internes avant d'agir.
L'accès direct = une seule écriture en mémoire, rien d'autre.
C'est ce qui est utilisé dans les drivers temps-réel.

## GPIO

| GPIO | Rôle |
|---|---|
| D2 | LED rouge |
| D4 | Bouton |

## Concepts

`soc/gpio_reg.h` · `GPIO_OUT_W1TS_REG` · `GPIO_OUT_W1TC_REG` ·
`REG_READ` · `REG_WRITE` · mémoire mappée
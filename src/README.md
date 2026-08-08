# OTA — Mise à jour firmware sans fil

Mise à jour du firmware ESP32 via WiFi depuis un navigateur,
sans brancher de câble USB. Intégré au projet IoT complet.

## Ce que ça fait

- Page `/ota` accessible depuis le dashboard web
- Sélectionne le fichier `firmware.bin` généré par PlatformIO
- L'ESP32 reçoit, vérifie et écrit le nouveau firmware dans la partition inactive
- Redémarre automatiquement sur le nouveau firmware
- Si le firmware est invalide → rollback automatique sur l'ancien

## Comment l'utiliser (vrai hardware)

```
1. Flasher l'ESP32 une première fois via USB
2. Récupérer l'IP dans le terminal série
3. Ouvrir http://<IP>/ota dans le navigateur
4. PlatformIO → Build
5. Sélectionner .pio/build/esp32dev/firmware.bin
6. Cliquer "Mettre à jour"
7. L'ESP32 redémarre avec le nouveau firmware
```

> Test sur vrai hardware uniquement — la flash physique ne peut pas être simulée dans Wokwi.

## Partition table

Fichier `partitions.csv` ajouté à la racine du projet.
Deux partitions OTA (`ota_0` / `ota_1`) de 1.25MB chacune.
L'ESP32 alterne entre les deux à chaque mise à jour.

## Concepts

`esp_ota_ops.h` · `esp_ota_begin` · `esp_ota_write` · `esp_ota_end` ·
`esp_ota_set_boot_partition` · `esp_restart` · partition table OTA · rollback automatique
# I2C — MPU6050 Accéléromètre + Gyroscope

Lecture d'un capteur MPU6050 via protocole I2C (SDA GPIO21, SCL GPIO22).
14 bytes lus en une seule transaction (burst read), affichage uniquement si changement détecté.

## Ce que ça fait

- Accéléromètre : X / Y / Z en g (±2g, 16384 LSB/g)
- Gyroscope : X / Y / Z en °/s (±250°/s, 131 LSB/°/s)
- Température interne en °C (formule datasheet)
- Log terminal uniquement si un axe dépasse son seuil de changement

## GPIO

| GPIO | Rôle |
|---|---|
| D21 | SDA — données I2C |
| D22 | SCL — horloge I2C |

## Concepts

`i2c_new_master_bus` · `i2c_master_bus_add_device` · `i2c_master_transmit_receive` · burst read 14 bytes · adresse I2C 0x68 · conversion raw int16 → unités physiques
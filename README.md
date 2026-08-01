# BLE — Serveur GATT + contrôle LED

L'ESP32 expose un service BLE via NimBLE.
Contrôle une LED depuis un smartphone avec l'app nRF Connect.

## Ce que ça fait

- Caractéristique READ → retourne l'état LED ("ON" / "OFF")
- Caractéristique WRITE → envoie "ON" ou "OFF" pour contrôler la LED D2
- Relance l'advertising automatiquement après déconnexion

## Test sur hardware réel

1. Flasher sur un vrai ESP32
2. Ouvrir **nRF Connect** (iOS / Android) → Scanner
3. Se connecter à **"ESP32-BLE"**
4. Trouver la caractéristique WRITE → envoyer `ON` ou `OFF`

> Build validé. Test BLE à faire sur vrai hardware — Wokwi ne simule pas le BLE natif ESP32.

## Concepts

NimBLE · `nimble_port_init` · `ble_gatt_svc_def` · UUID 128 bits ·
`ble_gap_adv_start` · GAP events · GATT READ/WRITE callbacks · task FreeRTOS dédiée BLE
# Pocket Watch

Firmware for a battery-powered smart alarm clock/watch, built on the
[Waveshare ESP32-S3-Touch-LCD-1.85B](BOARD.md) — a round 360×360 touchscreen
board with an ESP32-S3, RTC, IMU, audio codec, and battery fuel gauge.

It implements a bedside/pocket clock with a touch-driven LVGL UI: multiple
clock face themes, alarms with volume and scheduling controls, a countdown
timer, screen brightness/power management, device status (battery level),
and Wi-Fi-based time sync backed by the onboard PCF85063 RTC.

## Hardware

* **MCU**: ESP32-S3R8, dual-core @ 240 MHz, Wi-Fi + BLE 5, 16 MB flash, 8 MB PSRAM
* **Display**: 1.85" 360×360 TFT (ST77916, QSPI)
* **Touch**: CST816S capacitive touch (I²C)
* **Audio**: ES8311 codec (output) + ES7210 mic ADC (dual mic input)
* **Sensors**: QMI8658 6-axis IMU, PCF85063 RTC, BQ27220 battery fuel gauge
* **Storage**: MicroSD (SDMMC)
* **Power**: USB-C, 3.7V LiPo, hardware charging

See [BOARD.md](BOARD.md) for full hardware details.

## Firmware layout

| Module | Purpose |
|---|---|
| `main/app_alarm` | Alarm scheduling and firing logic |
| `main/app_timer` | Countdown timer logic |
| `main/app_clock` | LVGL UI: clock faces, alarm/timer/theme/brightness/time settings screens |
| `main/app_rtc` | PCF85063 RTC driver integration |
| `main/app_net` | Wi-Fi connection and NTP-based time sync |
| `main/app_msg` | System message / notification UI |
| `main/app_theme` | App-wide theming |
| `components/` | Board support package (display, touch, RTC, IMU) and audio extras |

## Building and flashing

Requires [ESP-IDF 5.5.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/).

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

## Battery optimization

Known battery-life findings and their status are tracked in
[BATTERY_OPTIMIZATIONS.md](BATTERY_OPTIMIZATIONS.md).

## License

This project's own code is licensed under the [MIT License](LICENSE).
Vendored code under `components/` and `managed_components/` (Espressif and
Waveshare packages, LVGL, etc.) retains its own upstream licenses.

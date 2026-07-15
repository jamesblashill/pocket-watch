# ESP32-S3-Touch-LCD-1.85B Reference

## Overview

Target board: **Waveshare ESP32-S3-Touch-LCD-1.85B**

This board is built around the Espressif **ESP32-S3** and includes a high-resolution touch display, audio hardware, motion sensors, battery management, and MicroSD support. Development is best done using **ESP-IDF 5.5.x**, which is the platform used by the official Waveshare examples.

---

# Hardware

## MCU

* ESP32-S3R8
* Dual-core Xtensa LX7 @ up to 240 MHz
* Wi-Fi 802.11 b/g/n
* Bluetooth 5 LE
* Native USB

## Memory

* 16 MB Flash
* 8 MB PSRAM

## Display

* 1.85" TFT LCD
* 360 × 360 resolution
* ST77916 controller
* QSPI interface

## Touch

* CST816S capacitive touch
* I²C

## Audio

* ES8311 audio codec (output)
* ES7210 microphone ADC (dual mic input)

## Sensors

* QMI8658 6-axis IMU
* PCF85063 RTC
* BQ27220 battery fuel gauge

## Storage

* MicroSD (SDMMC)

## Power

* USB-C
* 3.7V LiPo battery
* Hardware battery charging (firmware not required)

---

# Battery

The battery charging circuit operates independently of the ESP32.

Firmware **does not** manage charging.

Firmware may read battery information from the **BQ27220**, including:

* battery percentage
* voltage
* charging/discharging state
* current
* estimated runtime

---

# Primary Interfaces

## Display

* Controller: ST77916
* Bus: QSPI

## Touch

* Controller: CST816S
* Bus: I²C

## I²C Devices

* CST816S touch
* QMI8658 IMU
* ES8311 codec
* ES7210 ADC
* PCF85063 RTC
* BQ27220 battery gauge

---

# Recommended Software Stack

* ESP-IDF 5.5.x
* LVGL 8.x
* Waveshare Board Support Package (BSP)

---

# Development Workflow

Typical cycle:

1. Edit source
2. Build
3. Flash
4. Monitor serial output
5. Repeat

---

# Official Documentation

Board documentation

https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.85B

GitHub repository

https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85B

ESP-IDF Getting Started

https://docs.espressif.com/projects/esp-idf/

ESP-IDF VS Code Extension

https://docs.espressif.com/projects/vscode-esp-idf-extension/

---

# Useful Future Features

Potential capabilities supported by the hardware:

* Music player
* Audiobook player
* EPUB reader
* Battery monitoring
* Motion-based controls
* Bluetooth peripherals
* Wi-Fi synchronization
* OTA firmware updates
* Sleep/wake support
* MicroSD media library

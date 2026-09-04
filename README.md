# 🦊 FoxyHome

[![Platform](https://img.shields.io/badge/Platform-ESP8266%20%7C%20ESP32-E7352C.svg?logo=espressif&logoColor=white)](#modules)
[![Language](https://img.shields.io/badge/Language-C%2B%2B%20%7C%20Arduino-00979D.svg?logo=arduino&logoColor=white)](#modules)
[![Status](https://img.shields.io/badge/Status-Legacy%20Firmware-orange.svg)](#background)
[![Successor](https://img.shields.io/badge/Next%20Gen-Home%20Assistant%20%7C%20ESPHome-41BDF5.svg?logo=homeassistant&logoColor=white)](https://esphome.io/)

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-FFDD00?logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/resonaura)

Custom embedded C++ firmware suite for DIY smart home IoT hardware, sensors, and ambient lighting controllers.

---

## 🏡 Background & Evolution

This repository contains the custom bare-metal embedded firmware that previously powered my personal smart home hardware before migrating our entire home infrastructure to **Home Assistant** and native **ESPHome**.

While now maintained as historical reference code, each module represents a battle-tested standalone microcontroller solution featuring direct Wi-Fi networking, sensor telemetry, and local control.

---

## 📦 Firmware Modules

- **`ambilight/`**: Real-time multi-zone dynamic TV/monitor backlighting engine with WS2812B addressable LED strips.
- **`backlight/`**: Desk and furniture accent illumination controller with smooth PWM brightness fades and color transitions.
- **`climate-sensor/`**: Environmental monitoring node reading live room temperature, relative humidity, and atmospheric metrics over I2C/SPI.

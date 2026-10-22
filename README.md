# SMART LAB

ESP32 laboratory monitoring and safety system with a live online dashboard.

| | Link |
|---|---|
| 📊 **Dashboard** | https://smartlab.electrosparkzz.in/ |
| 🔌 **Circuit details** | https://smartlab.electrosparkzz.in/circuit/ |
| 💻 **Code & download** | https://smartlab.electrosparkzz.in/code/ |

## Features

- **DHT22**: temperature and humidity, with history charts
- **MQ-2**: gas/smoke level with alarm
- **Flame sensor**: fire detection with alarm
- **Two IR sensors**: people counter (A then B = entry, B then A = exit)
- **Buzzer**: local alarm that works even without internet
- **Online dashboard**: live data over public MQTT (HiveMQ), warning popup with siren
- **Wi-Fi switching**: the ESP32 scans nearby networks; pick one on the dashboard

## Wiring

| Sensor | ESP32 pin |
|---|---|
| DHT22 DATA | G4 |
| Flame D0 | G15 |
| IR A OUT (outside) | G18 |
| IR B OUT (inside) | G19 |
| Buzzer + | G25 |
| MQ-2 AO (via 10k/20k divider) | G34 |

Power: MQ-2 VCC → VIN (5 V); all other sensors → 3V3; all grounds → GND.
Full diagram and notes: [circuit page](https://smartlab.electrosparkzz.in/circuit/).

## Firmware

`firmware/smart_lab_mqtt/` is an Arduino sketch for the **ESP32 Dev Module**.

1. Copy `secrets.example.h` to `secrets.h` and enter your Wi-Fi (2.4 GHz only).
2. Install libraries: DHT sensor library, Adafruit Unified Sensor, PubSubClient, ArduinoJson.
3. Upload at **115200** baud.

`secrets.h` is ignored by git, so your Wi-Fi password is never uploaded.

## Project structure

```
index.html                 Dashboard
circuit/index.html         Circuit details
code/index.html            Code viewer and download
assets/site.css            Shared styles
firmware/smart_lab_mqtt/   ESP32 Arduino sketch
```

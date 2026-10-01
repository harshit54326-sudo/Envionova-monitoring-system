# EnvioNova — Real-time Environmental Monitoring System

EnvioNova is an embedded IoT system that measures **4 environmental parameters** (temperature, humidity, air quality and light), processes the readings in real time, and raises an alert the moment a safe limit is crossed. Alerts reach you **by SMS over GSM**, and every reading is logged to the cloud over **MQTT and HTTP** for remote monitoring.

![Architecture](docs/architecture.png)

## Why

Checking room conditions by hand is slow and easy to miss, and WiFi alone fails the moment the network drops. EnvioNova watches the environment continuously, filters out sensor noise so it doesn't raise false alarms, and uses a GSM link so critical alerts still reach a phone without internet.

## Features

- **Sensor-based data acquisition** — 4 parameters from 3 sensors: DHT22 (temperature, humidity), MQ135 (air quality), BH1750 (light in lux)
- **Real-time signal processing** — moving-average filter on the noisy MQ135 signal; if a sensor read fails, the last valid value is kept
- **Threshold-based alert logic** — each parameter has its own limits, with hysteresis so an alert doesn't flicker on and off at the boundary
- **GSM connectivity (SIM800L)** — SMS alert to a set phone number when a limit is crossed, with a cooldown so the same alert doesn't spam
- **Remote monitoring** — text `STATUS` to the device's SIM and it replies with the latest readings
- **Cloud logging** — JSON telemetry every 10 s over MQTT and HTTP POST, plus a separate MQTT topic for alert events
- **Local alert** — LED and buzzer stay on while any alert is active
- **Self-recovering connectivity** — WiFi and MQTT reconnect automatically after a drop

## Hardware

| Component | Purpose |
|---|---|
| ESP32 DevKit V1 | Microcontroller + WiFi |
| DHT22 | Temperature and humidity |
| MQ135 | Air quality (analog gas sensor) |
| BH1750 | Ambient light (I²C) |
| SIM800L + SIM card | GSM for SMS alerts and remote status |
| Active buzzer + LED | Local alert |
| 10kΩ resistor | DHT22 data pull-up |
| 10kΩ + 20kΩ resistors | Voltage divider for the MQ135 output |
| 3.7–4.2V, 2A supply | Separate power for SIM800L (Li-ion cell or buck converter) |

## Wiring

![Wiring](docs/wiring.png)

| Module pin | ESP32 pin |
|---|---|
| DHT22 DATA | GPIO4 (10kΩ pull-up to 3V3) |
| MQ135 AO | GPIO34 through a 10k/20k divider |
| MQ135 VCC | VIN (5V) |
| BH1750 SDA / SCL | GPIO21 / GPIO22 |
| BH1750 VCC | 3V3 |
| SIM800L TX / RX | GPIO16 (RX2) / GPIO17 (TX2) |
| Buzzer + | GPIO26 |
| Alert LED | GPIO2 (on-board) |
| All GND | GND (common ground) |

**Two things that commonly break this build:**
- The MQ135 runs on 5V and its analog output can go above 3.3V, the ESP32 ADC limit. The voltage divider keeps GPIO34 safe.
- The SIM800L draws up to 2A in short bursts while transmitting. Powering it from the ESP32 makes it reset randomly, so give it its own 3.7–4.2V supply and connect the grounds together.

## How it works

1. **Acquire** — all sensors are read every 2 seconds
2. **Process** — MQ135 values pass through a 10-sample moving average; failed DHT22/BH1750 reads fall back to the last good value
3. **Decide** — each reading is checked against its limit, with hysteresis
4. **Act**
   - When an alert **triggers**: LED and buzzer on, MQTT alert published, SMS sent (at most once per alert every 5 minutes)
   - When it **clears**: LED and buzzer off, MQTT "cleared" event published
   - Every 10 seconds: telemetry sent to the MQTT broker and the HTTP endpoint
5. **Listen** — incoming SMS are checked; a message containing `STATUS` gets an instant reply with current readings

## Default thresholds

| Parameter | Alert when |
|---|---|
| Temperature | above 35 °C or below 10 °C |
| Humidity | above 80% or below 20% |
| Air quality (MQ135 raw, 0–4095) | above 2600 (poor); 1800+ is reported as moderate |
| Light | below 50 lux |

All limits are in `config.h`. MQ135 output differs from one sensor to another, so calibrate the air-quality limits against your own module in clean air.

## Data format

Telemetry — topic `envionova/node01/telemetry` and HTTP POST body:

```json
{
  "device": "envionova-node-01",
  "uptime_s": 3600,
  "temperature_c": 29.4,
  "humidity_pct": 61.2,
  "air_raw": 1420,
  "air_level": "GOOD",
  "light_lux": 312.5
}
```

Alert event — topic `envionova/node01/alerts`:

```json
{ "alert": "AIR_POOR", "state": "TRIGGERED" }
```

SMS alert:

```
EnvioNova ALERT: AIR_POOR
EnvioNova status
Temp: 29.4 C
Humidity: 61.2 %
Air: POOR (2710)
Light: 312 lux
```

*(Values above illustrate the message format.)*

## Getting started

1. Install the **Arduino IDE** and add ESP32 support (Boards Manager → *esp32 by Espressif Systems*)
2. Install these libraries from Library Manager: **DHT sensor library**, **Adafruit Unified Sensor**, **BH1750** (by Christopher Laws), **PubSubClient**
3. Open `firmware/envionova/envionova.ino`
4. In `config.h`, fill in your WiFi details, MQTT broker, HTTP endpoint and the phone number for SMS alerts
5. Insert a SIM card with SMS balance into the SIM800L
6. Select **ESP32 Dev Module**, upload, and open Serial Monitor at **115200** baud
7. Let the MQ135 warm up for 2–3 minutes before trusting air-quality values

To see live cloud data, subscribe to `envionova/node01/#` from any MQTT client (for example MQTT Explorer) on the same broker. To check the device remotely, send an SMS with the text `STATUS` to its SIM number.

## Project structure

```
envionova-monitoring-system/
├── firmware/
│   └── envionova/
│       ├── envionova.ino   # main firmware
│       └── config.h        # WiFi, MQTT, GSM, pins, thresholds
├── docs/
│   ├── architecture.png
│   └── wiring.png
├── LICENSE
└── README.md
```

## Possible improvements

- Convert MQ135 raw values to calibrated ppm using the datasheet sensitivity curve
- Send cloud data over GPRS when WiFi is unavailable
- Deep sleep between readings for battery-powered deployments
- Live dashboard with Node-RED or Grafana
- Over-the-air (OTA) firmware updates

## Tech stack

ESP32 · Arduino (C++) · DHT22 · MQ135 · BH1750 · SIM800L (GSM) · I²C · UART · MQTT (PubSubClient) · HTTP

---
Built by **Harshit Kumar** · [LinkedIn](https://www.linkedin.com/in/harshit-kumar-47277737b) · [GitHub](https://github.com/harshit54326-sudo)

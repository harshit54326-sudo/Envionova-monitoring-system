// ============================================================
//  EnvioNova - configuration
//  Fill in your own values before uploading. Never commit real
//  WiFi passwords or phone numbers to a public repository.
// ============================================================
#pragma once

// ---------- WiFi (cloud logging) ----------
#define WIFI_SSID        "YOUR_WIFI_SSID"
#define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"

// ---------- MQTT ----------
#define MQTT_BROKER      "broker.hivemq.com"
#define MQTT_PORT        1883
#define MQTT_CLIENT_ID   "envionova-node-01"
#define MQTT_TOPIC_DATA  "envionova/node01/telemetry"
#define MQTT_TOPIC_ALERT "envionova/node01/alerts"

// ---------- HTTP cloud logging ----------
// Any endpoint that accepts a JSON POST (Flask/Node API,
// Google Apps Script web app, logging service, etc.)
#define HTTP_ENDPOINT    "http://your-server.example.com/api/readings"

// ---------- GSM (SIM800L) ----------
#define ALERT_PHONE      "+91XXXXXXXXXX"   // number that receives SMS alerts
#define SMS_COOLDOWN_MS  300000UL          // max one SMS per alert type every 5 min

// ---------- Pins (ESP32 DevKit V1) ----------
#define PIN_DHT          4     // DHT22 data
#define PIN_MQ135        34    // MQ135 analog out (through voltage divider)
#define PIN_I2C_SDA      21    // BH1750 SDA
#define PIN_I2C_SCL      22    // BH1750 SCL
#define PIN_GSM_RX       16    // ESP32 RX2  <- SIM800L TX
#define PIN_GSM_TX       17    // ESP32 TX2  -> SIM800L RX
#define PIN_LED_ALERT    2     // on-board LED
#define PIN_BUZZER       26    // active buzzer

// ---------- Timing ----------
#define SAMPLE_INTERVAL_MS   2000UL    // read sensors every 2 s
#define PUBLISH_INTERVAL_MS  10000UL   // log to cloud every 10 s

// ---------- Alert thresholds ----------
#define TEMP_HIGH_C       35.0f
#define TEMP_LOW_C        10.0f
#define HUMIDITY_HIGH     80.0f
#define HUMIDITY_LOW      20.0f
#define AIR_RAW_MODERATE  1800     // MQ135 raw ADC (0-4095); calibrate per sensor
#define AIR_RAW_POOR      2600
#define LUX_LOW           50.0f    // too dark

// Hysteresis: a reading must come back inside the limit by this margin
// before its alert clears, so alerts don't flicker at the boundary.
#define HYST_TEMP_C       1.0f
#define HYST_HUMIDITY     3.0f
#define HYST_AIR_RAW      100
#define HYST_LUX          10.0f

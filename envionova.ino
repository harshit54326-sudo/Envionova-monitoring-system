/*
 * EnvioNova - Real-time Environmental Monitoring System
 * ------------------------------------------------------
 * Measures 4 environmental parameters on an ESP32:
 *   temperature + humidity (DHT22), air quality (MQ135),
 *   light intensity (BH1750).
 *
 * - Samples every 2 s and smooths the noisy MQ135 signal
 * - Checks each reading against a threshold (with hysteresis)
 * - On a breach: LED + buzzer, MQTT alert, and an SMS over GSM
 * - Logs JSON telemetry to the cloud over MQTT and HTTP
 * - Remote monitoring: text "STATUS" to the SIM and it replies
 *   with the latest readings
 *
 * Libraries: DHT sensor library, Adafruit Unified Sensor,
 *            BH1750 (Christopher Laws), PubSubClient
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <DHT.h>
#include <BH1750.h>
#include "config.h"

// ================= Sensors & connectivity =================
DHT dht(PIN_DHT, DHT22);
BH1750 lightMeter;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
HardwareSerial gsm(2);   // UART2 -> SIM800L

// ================= Signal processing =================
// Moving average over the last AIR_WINDOW MQ135 samples.
const int AIR_WINDOW = 10;
int airBuffer[AIR_WINDOW];
int airIndex = 0;
bool airBufferFull = false;

int smoothAir(int raw) {
  airBuffer[airIndex] = raw;
  airIndex = (airIndex + 1) % AIR_WINDOW;
  if (airIndex == 0) airBufferFull = true;
  int count = airBufferFull ? AIR_WINDOW : airIndex;
  long sum = 0;
  for (int i = 0; i < count; i++) sum += airBuffer[i];
  return (int)(sum / count);
}

// ================= Readings =================
struct Reading {
  float temperature = NAN;
  float humidity    = NAN;
  int   airRaw      = 0;
  float lux         = NAN;
};
Reading current;

const char* airLevel(int raw) {
  if (raw >= AIR_RAW_POOR) return "POOR";
  if (raw >= AIR_RAW_MODERATE) return "MODERATE";
  return "GOOD";
}

// ================= Alerts =================
enum AlertId { TEMP_HIGH, TEMP_LOW, HUM_HIGH, HUM_LOW, AIR_POOR, LOW_LIGHT, ALERT_COUNT };
const char* ALERT_NAMES[ALERT_COUNT] = {
  "TEMP_HIGH", "TEMP_LOW", "HUM_HIGH", "HUM_LOW", "AIR_POOR", "LOW_LIGHT"
};
bool alertActive[ALERT_COUNT] = {false};
unsigned long lastSmsAt[ALERT_COUNT] = {0};

// State of an "upper limit" alert, with hysteresis.
bool checkHigh(bool active, float value, float limit, float hyst) {
  if (isnan(value)) return active;
  if (!active && value > limit) return true;
  if (active && value < limit - hyst) return false;
  return active;
}

// State of a "lower limit" alert, with hysteresis.
bool checkLow(bool active, float value, float limit, float hyst) {
  if (isnan(value)) return active;
  if (!active && value < limit) return true;
  if (active && value > limit + hyst) return false;
  return active;
}

// ================= GSM (SIM800L) =================
// Sends an AT command and waits for an expected reply.
bool gsmCommand(const char* cmd, const char* expect, unsigned long timeoutMs) {
  gsm.println(cmd);
  unsigned long start = millis();
  String reply;
  while (millis() - start < timeoutMs) {
    while (gsm.available()) reply += (char)gsm.read();
    if (reply.indexOf(expect) >= 0) return true;
    delay(10);
  }
  return false;
}

void gsmInit() {
  gsm.begin(9600, SERIAL_8N1, PIN_GSM_RX, PIN_GSM_TX);
  delay(3000);                                   // let the module boot
  bool ok = gsmCommand("AT", "OK", 2000);
  gsmCommand("AT+CMGF=1", "OK", 2000);           // SMS text mode
  gsmCommand("AT+CNMI=2,2,0,0,0", "OK", 2000);   // push new SMS straight to serial
  Serial.println(ok ? "[GSM] SIM800L ready" : "[GSM] no response - check power/wiring");
}

bool sendSms(const char* number, const String& text) {
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
  if (!gsmCommand(cmd, ">", 5000)) {
    Serial.println("[GSM] SMS prompt not received");
    return false;
  }
  gsm.print(text);
  gsm.write(26);                                 // Ctrl+Z sends the message
  unsigned long start = millis();
  String reply;
  while (millis() - start < 15000) {
    while (gsm.available()) reply += (char)gsm.read();
    if (reply.indexOf("+CMGS") >= 0) { Serial.println("[GSM] SMS sent"); return true; }
    if (reply.indexOf("ERROR") >= 0) break;
    delay(10);
  }
  Serial.println("[GSM] SMS failed");
  return false;
}

String statusText() {
  char buf[160];
  snprintf(buf, sizeof(buf),
           "EnvioNova status\nTemp: %.1f C\nHumidity: %.1f %%\nAir: %s (%d)\nLight: %.0f lux",
           current.temperature, current.humidity,
           airLevel(current.airRaw), current.airRaw, current.lux);
  return String(buf);
}

// Remote monitoring: reply to an incoming "STATUS" SMS with live readings.
// With AT+CNMI=2,2 an incoming SMS arrives as:
//   +CMT: "+91XXXXXXXXXX","","26/09/30,17:00:00+22"
//   STATUS
String pendingSender = "";

void handleIncomingGsm() {
  while (gsm.available()) {
    String line = gsm.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    if (line.startsWith("+CMT:")) {
      int first = line.indexOf('"');
      int second = line.indexOf('"', first + 1);
      pendingSender = (first >= 0 && second > first) ? line.substring(first + 1, second) : "";
    } else if (pendingSender.length() > 0) {
      String body = line;
      body.toUpperCase();
      if (body.indexOf("STATUS") >= 0) {
        Serial.printf("[GSM] STATUS request from %s\n", pendingSender.c_str());
        sendSms(pendingSender.c_str(), statusText());
      }
      pendingSender = "";
    }
  }
}

// ================= WiFi & MQTT =================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " connected" : " failed (will retry)");
}

void connectMQTT() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  Serial.print("[MQTT] Connecting... ");
  if (mqtt.connect(MQTT_CLIENT_ID)) Serial.println("ok");
  else Serial.printf("failed, rc=%d (will retry)\n", mqtt.state());
}

// ================= Sensor reading =================
void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) current.temperature = t;     // keep last good value on a bad read
  if (!isnan(h)) current.humidity = h;

  current.airRaw = smoothAir(analogRead(PIN_MQ135));

  float lux = lightMeter.readLightLevel();
  if (lux >= 0) current.lux = lux;
}

// ================= Alert evaluation =================
void onAlertChange(int id, bool nowActive) {
  char msg[96];
  snprintf(msg, sizeof(msg), "{\"alert\":\"%s\",\"state\":\"%s\"}",
           ALERT_NAMES[id], nowActive ? "TRIGGERED" : "CLEARED");
  Serial.printf("[ALERT] %s\n", msg);
  if (mqtt.connected()) mqtt.publish(MQTT_TOPIC_ALERT, msg);

  // SMS only when an alert triggers, and not more than once per cooldown.
  unsigned long now = millis();
  if (nowActive && (lastSmsAt[id] == 0 || now - lastSmsAt[id] >= SMS_COOLDOWN_MS)) {
    String text = String("EnvioNova ALERT: ") + ALERT_NAMES[id] + "\n" + statusText();
    if (sendSms(ALERT_PHONE, text)) lastSmsAt[id] = now;
  }
}

void evaluateAlerts() {
  bool next[ALERT_COUNT];
  next[TEMP_HIGH] = checkHigh(alertActive[TEMP_HIGH], current.temperature, TEMP_HIGH_C,   HYST_TEMP_C);
  next[TEMP_LOW]  = checkLow (alertActive[TEMP_LOW],  current.temperature, TEMP_LOW_C,    HYST_TEMP_C);
  next[HUM_HIGH]  = checkHigh(alertActive[HUM_HIGH],  current.humidity,    HUMIDITY_HIGH, HYST_HUMIDITY);
  next[HUM_LOW]   = checkLow (alertActive[HUM_LOW],   current.humidity,    HUMIDITY_LOW,  HYST_HUMIDITY);
  next[AIR_POOR]  = checkHigh(alertActive[AIR_POOR],  current.airRaw,      AIR_RAW_POOR,  HYST_AIR_RAW);
  next[LOW_LIGHT] = checkLow (alertActive[LOW_LIGHT], current.lux,         LUX_LOW,       HYST_LUX);

  bool anyActive = false;
  for (int i = 0; i < ALERT_COUNT; i++) {
    if (next[i] != alertActive[i]) {
      alertActive[i] = next[i];
      onAlertChange(i, next[i]);
    }
    anyActive = anyActive || alertActive[i];
  }
  digitalWrite(PIN_LED_ALERT, anyActive ? HIGH : LOW);
  digitalWrite(PIN_BUZZER,    anyActive ? HIGH : LOW);
}

// ================= Cloud logging =================
void publishReadings() {
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"device\":\"%s\",\"uptime_s\":%lu,"
    "\"temperature_c\":%.1f,\"humidity_pct\":%.1f,"
    "\"air_raw\":%d,\"air_level\":\"%s\",\"light_lux\":%.1f}",
    MQTT_CLIENT_ID, millis() / 1000,
    current.temperature, current.humidity,
    current.airRaw, airLevel(current.airRaw), current.lux);
  Serial.printf("[DATA] %s\n", payload);

  if (mqtt.connected()) mqtt.publish(MQTT_TOPIC_DATA, payload);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(HTTP_ENDPOINT);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);
    Serial.printf("[HTTP] POST -> %d\n", code);
    http.end();
  }
}

// ================= Setup / loop =================
unsigned long lastSample = 0;
unsigned long lastPublish = 0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_ALERT, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  analogReadResolution(12);                     // 0-4095
  analogSetPinAttenuation(PIN_MQ135, ADC_11db);

  dht.begin();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("[BH1750] not found - check wiring");
  }

  gsmInit();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  connectWiFi();
  connectMQTT();
  Serial.println("[EnvioNova] Running. MQ135 needs 2-3 min to warm up for stable values.");
}

void loop() {
  connectWiFi();
  connectMQTT();
  mqtt.loop();
  handleIncomingGsm();

  unsigned long now = millis();
  if (now - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample = now;
    readSensors();
    evaluateAlerts();
  }
  if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = now;
    publishReadings();
  }
}

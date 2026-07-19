#include <Arduino.h>
#include <SimpleDHT.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

WebServer server(80);

// ---------------- FreeRTOS variables -----------------
SemaphoreHandle_t serialMutex;    // prevents multiple tasks from printing to Serial at the same time

// ---------------- Soil moisture vairables -----------------
const int SENSOR_POWER_PIN = 6;   // GPIO that powers the sensor on/off
const int SENSOR_ADC_PIN   = 4;   // GPIO that reads the analog moisture value

const int RAW_DRY = 3500;   // raw value when soil is dry (HIGH now)
const int RAW_WET = 0;      // raw value when fully wet (LOW now)

volatile int soilPercent = -1;    // latest moisture reading as a percentage, shared across tasks
const unsigned long READ_INTERVAL_MS = 1800000; // how often to read the sensor (ms); 1800000 = 30 min for production

const int SENSOR_STABILIZE_MS = 200; // how long to wait after powering the sensor before reading it (ms)
const int SOIL_SAMPLE_COUNT  = 10;  // how many samples to take when reading the soil moisture sensor

// ---------------- DHT22 vairables -----------------
const int DHT_PIN = 18; // GPIO that reads the DHT22 sensor
SimpleDHT22 dht22(DHT_PIN); // Create an instance of the DHT22 sensor

// ---------------- Relay control variables -----------------
const int RELAY_PIN = 15;

#define RELAY_ON  LOW   // change to HIGH once new relay's active state is confirmed
#define RELAY_OFF HIGH

// const int MOISTURE_THRESHOLD = 99;                // TEST VALUE — change to 30 for production
// const unsigned long COOLDOWN_PERIOD_MS = 10000;    // TEST VALUE — change to real cooldown later (e.g. hours)
// const unsigned long WATERING_DURATION_MS = 4000;   // how long the pump stays on
// const unsigned long DECISION_INTERVAL_MS = 5000;   // TEST VALUE — how often we check the decision

int moistureThreshold = 30;   // in-memory copy, loaded from NVS at boot (fallback default: 30)
unsigned long cooldownPeriodMs = 6UL * 3600 * 1000;
unsigned long wateringDurationMs = 5000;
unsigned long decisionIntervalMs = 30UL * 60 * 1000;

volatile bool wateringActive = false;
volatile bool manualWaterRequest = false;

volatile float lastTemperature = 0;
volatile float lastHumidity = 0;

volatile unsigned long lastWateringTime = 0;

Preferences preferences;

const char* NVS_NAMESPACE = "plantcare";
const char* NVS_KEY_THRESHOLD = "threshold";

// ---------------- WiFi variables ----------------
const char* WIFI_SSID = "SSID";
const char* WIFI_PASSWORD = "PASSWORD";


// ==========================================================================================

// ---------------- NVS functions ----------------
void loadSettingsFromNVS() {
  preferences.begin(NVS_NAMESPACE, false);
  moistureThreshold = preferences.getInt(NVS_KEY_THRESHOLD, 30); // 30 = default if key not found
  preferences.end();

  xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.printf("Loaded moisture threshold from NVS: %d%%\n", moistureThreshold);
  xSemaphoreGive(serialMutex);
}

void saveThresholdToNVS(int newThreshold) {
  moistureThreshold = newThreshold;

  preferences.begin(NVS_NAMESPACE, false);
  preferences.putInt(NVS_KEY_THRESHOLD, moistureThreshold);
  preferences.end();

  xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.printf("Saved new moisture threshold to NVS: %d%%\n", moistureThreshold);
  xSemaphoreGive(serialMutex);
}

// --------------- Soil moisture functions -----------------
int rawToPercent(int raw) {
  int percent = map(raw, RAW_DRY, RAW_WET, 0, 100);
  return constrain(percent, 0, 100);
}

int readSoilAveraged(int samples) {
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(SENSOR_ADC_PIN);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return total / samples;
}

// -------------- Environment task -----------------
void environmentTask(void *parameter) {
  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW);

  for (;;) {
    float temperature = 0;
    float humidity = 0;
    int err = SimpleDHTErrSuccess;

    digitalWrite(SENSOR_POWER_PIN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(SENSOR_STABILIZE_MS));
    int raw = readSoilAveraged(SOIL_SAMPLE_COUNT);
    digitalWrite(SENSOR_POWER_PIN, LOW);
    soilPercent = rawToPercent(raw);

    bool dhtOk = (dht22.read2(&temperature, &humidity, NULL) == SimpleDHTErrSuccess);

    xSemaphoreTake(serialMutex, portMAX_DELAY);
    if (dhtOk) {
      Serial.printf("Soil: %d%% | Temp: %.2fC | Humidity: %.2f%%\n", soilPercent, temperature, humidity);
      
      lastTemperature = temperature;
      lastHumidity = humidity;
    } else {
      Serial.printf("Soil: %d%% | Temp: -- | Humidity: -- (DHT read failed)\n", soilPercent);
      
      lastTemperature = temperature;
    }
    xSemaphoreGive(serialMutex);

    vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL_MS));
  }
}

void relayControlTask(void *parameter) {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  for (;;) {
    bool soilIsDry = soilPercent < moistureThreshold;  // was: MOISTURE_THRESHOLD
    bool cooldownPassed = (millis() - lastWateringTime) > cooldownPeriodMs;

    if ((soilIsDry && cooldownPassed) || manualWaterRequest) {
      wateringActive = true;
      manualWaterRequest = false;

      xSemaphoreTake(serialMutex, portMAX_DELAY);
      Serial.println("Soil is dry, starting watering cycle");
      xSemaphoreGive(serialMutex);

      digitalWrite(RELAY_PIN, RELAY_ON);
      vTaskDelay(pdMS_TO_TICKS(wateringDurationMs));
      digitalWrite(RELAY_PIN, RELAY_OFF);

      lastWateringTime = millis();

      xSemaphoreTake(serialMutex, portMAX_DELAY);
      Serial.println("Watering cycle complete");
      xSemaphoreGive(serialMutex);

      wateringActive = false;
    } else if (soilIsDry && !cooldownPassed) {
      xSemaphoreTake(serialMutex, portMAX_DELAY);
      Serial.println("Soil is dry but still in cooldown period");
      xSemaphoreGive(serialMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(decisionIntervalMs));
  }
}

// ---------------- WebServer task ----------------
void webServerTask(void *parameter) {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/water", HTTP_POST, handleWaterNow);
  server.begin();

  xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.println("Web server started");
  xSemaphoreGive(serialMutex);

  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// ---------------- WiFi connection function ----------------
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.print("Connecting to WiFi");
  xSemaphoreGive(serialMutex);

  while(WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }

  xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.println();
  Serial.print("Connected. Dashboard available at: http://");
  Serial.println(WiFi.localIP());
  xSemaphoreGive(serialMutex);
};

// ---------------- WebServer routes handlers ----------------
void handleRoot() {
  File file = LittleFS.open("/dashboard.html", "r");
  if (!file) {
    server.send(500, "text/plain", "Dashboard file not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleStatus() {
  JsonDocument doc;

  doc["soilPercent"] = soilPercent;
  doc["temperature"] = lastTemperature;
  doc["humidity"] = lastHumidity;
  doc["relayOn"] = (digitalRead(RELAY_PIN) == RELAY_ON);
  doc["wateringActive"] = wateringActive;

  unsigned long elapsed = millis() - lastWateringTime;
  doc["cooldownRemainingMs"] = (elapsed < cooldownPeriodMs) ? (cooldownPeriodMs - elapsed) : 0;
  doc["lastWateredMsAgo"] = elapsed;

  doc["moistureThreshold"] = moistureThreshold;
  doc["cooldownPeriodMs"] = cooldownPeriodMs;
  doc["wateringDurationMs"] = wateringDurationMs;
  doc["decisionIntervalMs"] = decisionIntervalMs;
  doc["uptimeMs"] = millis();

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleSettings() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"missing body\"}");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }

  if (doc["moistureThreshold"].is<int>()) {
    saveThresholdToNVS(doc["moistureThreshold"].as<int>());
  }
  if (doc["cooldownPeriodMs"].is<unsigned long>()) {
    cooldownPeriodMs = doc["cooldownPeriodMs"].as<unsigned long>();
  }
  if (doc["wateringDurationMs"].is<unsigned long>()) {
    wateringDurationMs = doc["wateringDurationMs"].as<unsigned long>();
  }
  if (doc["decisionIntervalMs"].is<unsigned long>()) {
    decisionIntervalMs = doc["decisionIntervalMs"].as<unsigned long>();
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleWaterNow() {
  manualWaterRequest = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting soil moisture sensor test (FreeRTOS)...");

  loadSettingsFromNVS();
  lastWateringTime = millis(); // conservative default until real time (NTP) is available in Phase 8

  serialMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(environmentTask, "EnvironmentTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(relayControlTask, "RelayControlTask", 4096, NULL, 1, NULL, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
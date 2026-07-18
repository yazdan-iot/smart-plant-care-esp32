#include <Arduino.h>
#include <SimpleDHT.h>

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

const int MOISTURE_THRESHOLD = 99;                // TEST VALUE — change to 30 for production
const unsigned long COOLDOWN_PERIOD_MS = 10000;    // TEST VALUE — change to real cooldown later (e.g. hours)
const unsigned long WATERING_DURATION_MS = 4000;   // how long the pump stays on
const unsigned long DECISION_INTERVAL_MS = 5000;   // TEST VALUE — how often we check the decision

volatile unsigned long lastWateringTime = 0;

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
    } else {
      Serial.printf("Soil: %d%% | Temp: -- | Humidity: -- (DHT read failed)\n", soilPercent);
    }
    xSemaphoreGive(serialMutex);

    vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL_MS));
  }
}

void relayControlTask(void *parameter) {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  for (;;) {
    bool soilIsDry = soilPercent < MOISTURE_THRESHOLD;
    bool cooldownPassed = (millis() - lastWateringTime) > COOLDOWN_PERIOD_MS;

    if (soilIsDry && cooldownPassed) {
      xSemaphoreTake(serialMutex, portMAX_DELAY);
      Serial.println("Soil is dry, starting watering cycle");
      xSemaphoreGive(serialMutex);

      digitalWrite(RELAY_PIN, RELAY_ON);
      vTaskDelay(pdMS_TO_TICKS(WATERING_DURATION_MS));
      digitalWrite(RELAY_PIN, RELAY_OFF);

      lastWateringTime = millis();

      xSemaphoreTake(serialMutex, portMAX_DELAY);
      Serial.println("Watering cycle complete");
      xSemaphoreGive(serialMutex);

    } else if (soilIsDry && !cooldownPassed) {
      xSemaphoreTake(serialMutex, portMAX_DELAY);
      Serial.println("Soil is dry but still in cooldown period");
      xSemaphoreGive(serialMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(DECISION_INTERVAL_MS));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting soil moisture sensor test (FreeRTOS)...");

  serialMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(environmentTask, "EnvironmentTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(relayControlTask, "RelayControlTask", 4096, NULL, 1, NULL, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
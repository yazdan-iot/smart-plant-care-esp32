#include <Arduino.h>

const int SENSOR_POWER_PIN = 6;   // GPIO that powers the sensor on/off
const int SENSOR_ADC_PIN   = 4;   // GPIO that reads the analog moisture value

const int RAW_DRY = 3500;   // raw value when soil is dry (HIGH now)
const int RAW_WET = 0;      // raw value when fully wet (LOW now)

volatile int soilPercent = -1;    // latest moisture reading as a percentage, shared across tasks

SemaphoreHandle_t serialMutex;    // prevents multiple tasks from printing to Serial at the same time

const unsigned long READ_INTERVAL_MS = 1800000; // how often to read the sensor (ms); 1800000 = 30 min for production

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

void soilSensorTask(void *parameter) {
  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW);

  for (;;) {
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(200));

    int raw = readSoilAveraged(10);
    digitalWrite(SENSOR_POWER_PIN, LOW);

    soilPercent = rawToPercent(raw);


    xSemaphoreTake(serialMutex, portMAX_DELAY);
    Serial.printf("Raw: %d | Moisture: %d%%\n", raw, soilPercent);
    xSemaphoreGive(serialMutex);

    vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL_MS));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting soil moisture sensor test (FreeRTOS)...");

  serialMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(soilSensorTask, "SoilSensorTask", 2048, NULL, 1, NULL, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
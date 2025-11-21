#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "RingBuffer.h"
#include "ThingsBoardClient.h"
#include "SdModule.h"
#include "WiFiManager.h"
#include "WebServerModule.h"
#include "TimeManager.h"
#include "GpsModule.h"
#include "esp_sleep.h"

SET_TIME_BEFORE_STARTING_SKETCH_MS(5000);

// --- Wi-Fi settings ---
std::vector<WiFiConfig> WIFI_CONFIG = {
    {"KTO-Rosomak", "12345678"},
    {"siec1", "haslo1"},
    {"siec2", "haslo2"}
};

WiFiManager wifiManager(WIFI_CONFIG);
TaskHandle_t wifiTaskHandle = nullptr;

// --- ThingsBoard settings ---
const char* THINGSBOARD_SERVER = "demo.thingsboard.io";
const int THINGSBOARD_PORT = 1883;
const char* MQTT_CLIENT_ID = "esp32_test";
const char* MQTT_USERNAME  = "user123";
const char* MQTT_PASSWORD  = "haslo123";
ThingsBoardClient tbClient(THINGSBOARD_SERVER, THINGSBOARD_PORT, MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
TaskHandle_t thingsboardTaskHandle = NULL;

// --- SD card settings ---
const uint8_t SD_CS_PIN = 5;
SdModule sdModule(SD_CS_PIN);
TaskHandle_t sdModuleTaskHandle = NULL;

SemaphoreHandle_t sdSignal = NULL; // kept for compatibility (not used for pipeline)

// Pipeline configuration (parameterize sizes)
const int RINGBUFFER_CAPACITY = 10; // change as needed
const int BATCH_SIZE = 2; // number of records that trigger SD write

// Task handles for notifications
TaskHandle_t coordinatorTaskHandle = NULL;
TaskHandle_t buttonTaskHandle = NULL;
TaskHandle_t goToSleepTaskHandle = NULL;

// Flags used during forced flush/sleep sequence
volatile bool sdForcedFlag = false;
volatile bool goToSleepRequested = false;
// WiFi event semaphore (used by WiFiTask)
SemaphoreHandle_t wifiEventSem = NULL;

// --- GPS settings ---
const int GPS_RX_PIN = 16;
const int GPS_TX_PIN = 17;
const int GPS_BAUDRATE = 115200;
const int PPS_PIN = 32;
TaskHandle_t gpsModuleTaskHandle = NULL;

// --- DS18B20 settings ---
#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);
TaskHandle_t tempModuleTaskHandle = NULL;

// --- Delays ---
const int Delay_GPS = 15000;
const int Delay_Temp = 15000;
const int Delay_SD = 15000;
const int Delay_TB = 30000; // dłuższy interwał dla TB

// --- Data structure ---
enum TimeSource : uint8_t { TIME_GPS = 0, TIME_WIFI = 1, TIME_LOCAL = 2 };
#define ERR_GPS_NO_FIX   (1 << 0)
#define ERR_TEMP_FAIL    (1 << 1)
#define ERR_SD_FAIL      (1 << 2)
#define ERR_TB_FAIL      (1 << 3)

struct SensorData {
  double lat;
  double lon;
  double elevation;
  double speed;
  float temp;
  uint64_t timestamp;
  TimeSource timestamp_time_source;
  uint64_t last_gps_fix_timestamp;
  uint64_t last_temp_read_timestamp;
  uint8_t error_code;
  bool tb_sent; 
};
SensorData data;
SemaphoreHandle_t dataSem;
RingBuffer<SensorData> ringBuffer(RINGBUFFER_CAPACITY);

const int LED_PIN_1 = 25;
const int LED_PIN_2 = 26;
const int LED_PIN_3 = 27;

#define WAKE_BUTTON_PIN 33

volatile bool shouldGoToSleep = false;
unsigned long lastButtonPressTime = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 300;

void IRAM_ATTR handleWakeISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (buttonTaskHandle) vTaskNotifyGiveFromISR(buttonTaskHandle, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

// ButtonTask: waits for ISR notification, debounces and signals GoToSleepTask via notification
void ButtonTask(void* pvParameters) {
    for (;;) {
        // wait for ISR notification
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) > 0) {
            unsigned long now = millis();
            if (now - lastButtonPressTime > BUTTON_DEBOUNCE_MS) {
                lastButtonPressTime = now;
                Serial.println("[BUTTON] ISR detected (notif), signaling GoToSleepTask");
                goToSleepRequested = true;
                if (goToSleepTaskHandle) xTaskNotifyGive(goToSleepTaskHandle);
            }
        }
    }
}

// --- TASK: GPS ---
void TaskGPS(void* pvParameters){
    for(;;){
        // wait until coordinator notifies this task
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) > 0) {
            gpsWake();
            bool gpsOk = false;
            unsigned long start = millis();

            while(millis()-start < 5000) {
              gpsOk = gpsRead() || gpsOk;
              vTaskDelay(5);
            }

            uint64_t now = TimeManager::getTimestampMs();

            xSemaphoreTake(dataSem, portMAX_DELAY);
            if(gpsOk && gpsHasFix()){
                TimeManager::updateFromGps();
                double lat, lon, elev, speed;
                getGpsData(lat, lon, elev, speed);
                data.lat = lat;
                data.lon = lon;
                data.elevation = elev;
                data.speed = speed;
                data.last_gps_fix_timestamp = now;
                data.error_code &= ~ERR_GPS_NO_FIX;
                digitalWrite(LED_PIN_2, HIGH);
            } else {
                data.error_code |= ERR_GPS_NO_FIX;
                digitalWrite(LED_PIN_2, LOW);
            }
            xSemaphoreGive(dataSem);

            // notify coordinator that GPS read finished
            if (coordinatorTaskHandle) xTaskNotifyGive(coordinatorTaskHandle);
        }
    }
}

// --- TASK: Temperature ---
void TaskTemp(void* pvParameters){
    for(;;){
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) > 0) {
            tempSensor.requestTemperatures();
            float tempC = tempSensor.getTempCByIndex(0);
            uint64_t now = TimeManager::getTimestampMs();

            xSemaphoreTake(dataSem, portMAX_DELAY);
            if(tempC != DEVICE_DISCONNECTED_C && tempC>-55 && tempC<125){
                data.temp = tempC; data.last_temp_read_timestamp = now;
                data.error_code &= ~ERR_TEMP_FAIL;
            } else {
                data.error_code |= ERR_TEMP_FAIL;
            }
            xSemaphoreGive(dataSem);

            // notify coordinator that Temp read finished
            if (coordinatorTaskHandle) xTaskNotifyGive(coordinatorTaskHandle);
        }
    }
}

// --- TASK: SD Logging ---
void TaskSD(void* pvParameters){
    SensorData batch[BATCH_SIZE];
    for(;;){
        // Wait until notified to process (either normal batch or forced flush via sdForcedFlag)
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(Delay_SD)) == 0) {
            continue;
        }

        bool forced = false;
        if (sdForcedFlag) {
            forced = true;
            sdForcedFlag = false;
        }

        int count = 0;
        // If not forced, only pop when at least BATCH_SIZE available
        if (!forced) {
            if (ringBuffer.size() < BATCH_SIZE) {
                continue;
            }
        }
        count = ringBuffer.popBatch(batch, BATCH_SIZE);
        if(count>0){
            // convert SensorData[] -> JsonArray and pass to SdModule
            StaticJsonDocument<16384> doc;
            JsonArray arr = doc.to<JsonArray>();
            for (int i = 0; i < count; ++i) {
                JsonObject o = arr.createNestedObject();
                o["lat"] = batch[i].lat;
                o["lon"] = batch[i].lon;
                o["elevation"] = batch[i].elevation;
                o["speed"] = batch[i].speed;
                o["temp"] = batch[i].temp;
                o["timestamp"] = batch[i].timestamp;
                o["time_source"] = (int)batch[i].timestamp_time_source;
                o["last_gps_fix_timestamp"] = batch[i].last_gps_fix_timestamp;
                o["last_temp_read_timestamp"] = batch[i].last_temp_read_timestamp;
                o["error_code"] = batch[i].error_code;
                o["tb_sent"] = batch[i].tb_sent;
            }
            bool ok = false;
            try { ok = sdModule.appendRecords(arr); } catch(...) { ok = false; }

            if(!ok){
                digitalWrite(LED_PIN_3, LOW);
                Serial.println("[SD][ERR] Append failed!");
                xSemaphoreTake(dataSem, portMAX_DELAY);
                data.error_code |= ERR_SD_FAIL;
                xSemaphoreGive(dataSem);
            } else {
                // notify TB task that SD has new data
                if (thingsboardTaskHandle) xTaskNotifyGive(thingsboardTaskHandle);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(Delay_SD));
    }
}

// --- TASK: ThingsBoard ---
void TaskTB(void* pvParameters){
    for(;;){
        // Wait for SD notification (new batch) or timeout
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(Delay_TB)) > 0) {
            // sdSignal taken -> try to send
            if (WiFi.status()==WL_CONNECTED) {
                digitalWrite(LED_PIN_3, HIGH);
                int sent = tbClient.sendBatchToTB(sdModule, BATCH_SIZE);
                (void)sent;
                // If sleep was requested, notify GoToSleepTask that TB finished
                if (goToSleepRequested && goToSleepTaskHandle) {
                    goToSleepRequested = false;
                    xTaskNotifyGive(goToSleepTaskHandle);
                }
            } else {
                // try to reconnect
                if (wifiManager.connectToBest()) {
                    int sent = tbClient.sendBatchToTB(sdModule, BATCH_SIZE);
                    (void)sent;
                    if (goToSleepRequested && goToSleepTaskHandle) {
                        goToSleepRequested = false;
                        xTaskNotifyGive(goToSleepTaskHandle);
                    }
                }
            }
        }
        // loop back; TB mostly driven by notification
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Coordinator: prepares timestamp, triggers sensor reads, waits for completion and pushes snapshot to ring buffer
void CoordinatorTask(void* pvParameters) {
    for (;;) {
        // Prepare timestamp
        uint64_t now = TimeManager::getTimestampMs();
        xSemaphoreTake(dataSem, portMAX_DELAY);
        data.timestamp = now;
        data.timestamp_time_source = (WiFi.status()==WL_CONNECTED && TimeManager::isSynchronized())?TIME_WIFI:TIME_LOCAL;
        xSemaphoreGive(dataSem);

        // Trigger GPS and Temp tasks via notifications
        if (gpsModuleTaskHandle) xTaskNotifyGive(gpsModuleTaskHandle);
        if (tempModuleTaskHandle) xTaskNotifyGive(tempModuleTaskHandle);

        // Wait for both to finish (expect two notifications from gps/temp)
        int done = 0;
        for (int i = 0; i < 2; ++i) {
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(6000)) > 0) done++;
        }

        // Build a snapshot and push to ring buffer
        xSemaphoreTake(dataSem, portMAX_DELAY);
        SensorData snapshot = data;
        xSemaphoreGive(dataSem);

        if (!ringBuffer.push(snapshot)) {
            Serial.println("[COORD] RingBuffer full, dropping snapshot");
        }

        // If enough items accumulated, notify SD task to persist
        if (ringBuffer.size() >= BATCH_SIZE && sdModuleTaskHandle) {
            xTaskNotifyGive(sdModuleTaskHandle);
        }

        vTaskDelay(pdMS_TO_TICKS(Delay_GPS));
    }
}

// GoToSleepTask: orchestrates forced SD flush and TB send then sleeps
void GoToSleepTask(void* pvParameters) {
    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) > 0) {
            Serial.println("[SLEEP TASK] Starting graceful shutdown: flush SD and TB");

            // Request SD to flush remaining records (forced mode)
            sdForcedFlag = true;
            if (sdModuleTaskHandle) xTaskNotifyGive(sdModuleTaskHandle);

            // mark that we're waiting for TB to finish
            goToSleepRequested = true;

            // Wait for TB to signal completion (notified by TB task), timeout 10s
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000)) == 0) {
                Serial.println("[SLEEP TASK] Timeout waiting for TB, continuing to sleep");
            } else {
                Serial.println("[SLEEP TASK] TB signaled completion");
            }

            // prepare wakeup
            Serial.println("[SLEEP TASK] Preparing to enter deep sleep");
            esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BUTTON_PIN, 0);
            delay(100);
            esp_deep_sleep_start();
        }
    }
}

// --- Task wołający logikę z WiFiController ---
void WiFiTask(void* pvParameters) {
    for (;;) {
        // react to WiFi events or periodically check
        if (wifiEventSem) {
            if (xSemaphoreTake(wifiEventSem, pdMS_TO_TICKS(Delay_TB)) == pdTRUE) {
                // an event occurred - check status
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("[WiFiTask] WiFi connected event");
                    digitalWrite(LED_PIN_1, HIGH);
                } else {
                    Serial.println("[WiFiTask] WiFi disconnected event");
                    digitalWrite(LED_PIN_1, LOW);
                    // try to reconnect
                    if (wifiManager.connectToBest()) {
                        Serial.println("[WiFiTask] Reconnected to WiFi after event");
                    }
                }
            } else {
                // timeout - perform health check
                if (!wifiManager.isConnected()) {
                    Serial.println("[WiFiTask] Periodic check - not connected, attempting reconnect...");
                    wifiManager.connectToBest();
                }
            }
        } else {
            // no wifiEventSem - fallback to periodic reconnect attempts
            if (!wifiManager.isConnected()) {
                Serial.println("[WiFiTask] Not connected, attempting to reconnect...");
                wifiManager.connectToBest();
            }
            vTaskDelay(pdMS_TO_TICKS(Delay_TB));
        }
    }
}

void goToDeepSleep() {
    Serial.println("[SLEEP] Preparing for deep sleep...");
    Serial.println("[SLEEP] Please release the button...");
    
    // Czekaj aż przycisk zostanie zwolniony (HIGH = zwolniony, INPUT_PULLUP)
    unsigned long releaseWaitStart = millis();
    while (digitalRead(WAKE_BUTTON_PIN) == LOW && millis() - releaseWaitStart < 2000) {
        delay(50);
    }
    
    if (digitalRead(WAKE_BUTTON_PIN) == LOW) {
        Serial.println("[SLEEP] Button still pressed after 2 seconds. Aborting sleep.");
        return;
    }
    
    Serial.println("[SLEEP] Button released. Waiting for debounce...");
    delay(500);  // Czekaj na ustabilizowanie się sygnału

    // zezwól na wybudzenie przyciskiem
    esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BUTTON_PIN, 0);

    // jeśli chcesz dodatkowy timer:
    // esp_sleep_enable_timer_wakeup(10 * 60 * 1000000ULL);

    Serial.println("[SLEEP] Going to sleep.");
    delay(100);
    // Before sleeping, flush any remaining buffered data to SD and attempt TB send
    {
        SensorData batch[10];
        // pop remaining batches and append
        while (ringBuffer.size() > 0) {
            int cnt = ringBuffer.popBatch(batch, 10);
            if (cnt <= 0) break;

            StaticJsonDocument<16384> doc;
            JsonArray arr = doc.to<JsonArray>();
            for (int i = 0; i < cnt; ++i) {
                JsonObject o = arr.createNestedObject();
                o["lat"] = batch[i].lat;
                o["lon"] = batch[i].lon;
                o["elevation"] = batch[i].elevation;
                o["speed"] = batch[i].speed;
                o["temp"] = batch[i].temp;
                o["timestamp"] = batch[i].timestamp;
                o["time_source"] = (int)batch[i].timestamp_time_source;
                o["last_gps_fix_timestamp"] = batch[i].last_gps_fix_timestamp;
                o["last_temp_read_timestamp"] = batch[i].last_temp_read_timestamp;
                o["error_code"] = batch[i].error_code;
                o["tb_sent"] = batch[i].tb_sent;
            }
            bool ok = false;
            try { ok = sdModule.appendRecords(arr); } catch(...) { ok = false; }
            if (!ok) Serial.println("[SLEEP][SD] Failed to save batch before sleep");
        }

        // Try to connect WiFi and send TB batch
        if (wifiManager.connectToBest()) {
            if (tbClient.isConnected() || tbClient.connect()) {
                int sent = tbClient.sendBatchToTB(sdModule, 500);
                (void)sent;
            }
        }
    }

    esp_deep_sleep_start();
}


// --- SETUP ---
void setup(){
    Serial.begin(115200);

    esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    if (wakeCause == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("[WAKE] Woke up from button");
    } else {
        Serial.println("[BOOT] Cold start");
    }

    Serial.println("[INIT] Starting...");

    pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
    // Button handled via ISR -> task (notifications used)
    sdSignal = xSemaphoreCreateBinary();
    wifiEventSem = xSemaphoreCreateBinary();

    dataSem = xSemaphoreCreateMutex();

    if (sdModule.begin()){
        digitalWrite(LED_PIN_3, HIGH);
    }
    else{
        digitalWrite(LED_PIN_3, LOW);
    }

    wifiManager.begin();

    // Register WiFi event -> notify wifiEventSem
    if (wifiEventSem) {
        WiFi.onEvent([](WiFiEvent_t event){
            (void)event;
            if (wifiEventSem) xSemaphoreGive(wifiEventSem);
        });
    }

    if(!wifiManager.connectToBest()) {
        Serial.println("[MAIN] Could not connect to any AP, retrying later...");
    }

    pinMode(LED_PIN_1, OUTPUT);
    pinMode(LED_PIN_2, OUTPUT);
    pinMode(LED_PIN_3, OUTPUT);
    
    
    gpsInit(GPS_BAUDRATE, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("[GPS] Initialized");

    tempSensor.begin();
    Serial.println("[TEMP] DS18B20 initialized");

    TimeManager::begin(PPS_PIN);

    // Opcjonalnie NTP jako backup
    TimeManager::enableNtpBackup("pool.ntp.org", "time.google.com", "time.cloudflare.com");

    xTaskCreate(TaskGPS,  "GPSTask",  8192, NULL, 1,  &gpsModuleTaskHandle);
    xTaskCreate(TaskTemp, "TempTask", 8192, NULL, 1,  &tempModuleTaskHandle);
    xTaskCreate(TaskSD,   "SDTask",   16384, NULL, 1,  &sdModuleTaskHandle);
    xTaskCreate(TaskTB,   "TBTask",   16384, NULL, 1,  &thingsboardTaskHandle);
    xTaskCreate(WiFiTask, "WiFiTask", 8192, NULL, 1,  &wifiTaskHandle);
    xTaskCreate(ButtonTask, "ButtonTask", 4096, NULL, 1, &buttonTaskHandle);
    xTaskCreate(CoordinatorTask, "Coordinator", 8192, NULL, 2, &coordinatorTaskHandle);
    xTaskCreate(GoToSleepTask, "GoToSleep", 8192, NULL, 2, &goToSleepTaskHandle);

    // attach ISR for button
    attachInterrupt(digitalPinToInterrupt(WAKE_BUTTON_PIN), handleWakeISR, FALLING);

    startWebServer(80);
    Serial.println("[WEB] Server started");
}



void loop() {

}


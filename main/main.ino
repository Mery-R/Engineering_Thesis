#include <WiFi.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
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
    {"Dom2", "xyz"},
    {"Hotspot", "..." }
};

WiFiManager wifiManager(WIFI_CONFIG);

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
const int RINGBUFFER_CAPACITY = 2; // change as needed
const int BATCH_SIZE = 2; // number of records that trigger SD write

// Task handles for notifications
TaskHandle_t coordinatorTaskHandle = NULL;
TaskHandle_t buttonTaskHandle = NULL;
TaskHandle_t goToSleepTaskHandle = NULL;

// Flags used during forced flush/sleep sequence
volatile bool sdForcedFlag = false;
volatile bool tbForcedFlag = false;  // Flag to force TB send via RPC
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
unsigned long lastSDSend = 0;
const unsigned long sdSendInterval = 1000; // 1 sekunda między wysyłkami SD

// --- Data structure ---
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

// -----------------------------------------------------
// -------------------- Functions ----------------------
// -----------------------------------------------------

// ISR for wake button
void IRAM_ATTR handleWakeISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (buttonTaskHandle) vTaskNotifyGiveFromISR(buttonTaskHandle, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}


// RPC Callback for forced send
void rpcForceCallback(bool forced) {
    if (forced) {
        Serial.println("[RPC] Force send requested");
        tbForcedFlag = true;
        if (thingsboardTaskHandle) xTaskNotifyGive(thingsboardTaskHandle);
    }
}

// -----------------------------------------------------
// ----------------------- TASKS -----------------------
// -----------------------------------------------------

// Coordinator: prepares timestamp, triggers sensor reads, waits for completion and pushes snapshot to ring buffer
void CoordinatorTask(void* pvParameters) {
    for (;;) {
        // Prepare timestamp
        uint64_t now = TimeManager::getTimestampMs();
        xSemaphoreTake(dataSem, portMAX_DELAY);
        data.timestamp = now;
        data.timestamp_time_source = TimeManager::getTimeSource();
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

// WifiEventHandler: handles WiFi events
void WiFiEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    WiFiManager* mgr = static_cast<WiFiManager*>(arg);

    if (base == WIFI_EVENT) {
        switch (id) {

        // Station started
        case WIFI_EVENT_STA_START:
            Serial.println("[WiFiEvent] Connecting...");
            mgr->connectToBest();
            break;
        
        // Station connected
        case WIFI_EVENT_STA_CONNECTED:
            Serial.println("[WiFiEvent] Connected");
            digitalWrite(LED_PIN_1, HIGH);
            break;

        // Station disconnected
        case WIFI_EVENT_STA_DISCONNECTED:
            Serial.println("[WiFiEvent] Dicsonnected");
            digitalWrite(LED_PIN_1, LOW);
            mgr->connectToBest();
            break;

        }
    }

    // Print IP address when got IP
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = (ip_event_got_ip_t*)data;
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());

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

        if (ringBuffer.size() >= BATCH_SIZE){
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
                digitalWrite(LED_PIN_3, HIGH);
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
        bool forced = false;

        // Sprawdzenie notyfikacji lub forced send
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) > 0) {
            if (tbForcedFlag) {
                forced = true;
                tbForcedFlag = false;
            }
        }

        if (WiFi.status() == WL_CONNECTED) {
            if (!tbClient.isConnected()) tbClient.connect();

            if (tbClient.isConnected()) {
                unsigned long now = millis();

                // 1. Wysyłka z ring buffer jeśli pełny lub forced
                if (forced || ringBuffer.size() >= BATCH_SIZE) {
                    SensorData batch[BATCH_SIZE];
                    int count = ringBuffer.popBatch(batch, BATCH_SIZE);
                    if (count > 0) {
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
                        }
                        if (tbClient.sendBatchDirect(arr)){
                            Serial.printf("[TB] Sent %d records from RingBuffer\n", count);
                        } else {
                            Serial.println("[TB][ERR] Failed to send batch from RingBuffer");
                        }
                    }
                }

                // 2. Wysyłka danych z SD jeśli są niewysłane, co sdSendInterval
                if (now - lastSDSend >= sdSendInterval) {
                    if (tbClient.sendUnsent(sdModule, BATCH_SIZE) > 0) {
                        lastSDSend = now;
                    }
                }
            }
        } else {
            if (!wifiManager.isConnected()) wifiManager.connectToBest();
        }

        // Sleep task
        if (goToSleepRequested && forced) {
            goToSleepRequested = false;
            xTaskNotifyGive(goToSleepTaskHandle);
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // krótki delay, pętla reaguje szybko
    }
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

// GoToSleepTask: waits for notification from ButtonTask, performs flushes and enters deep sleep
// --- TASK: GoToSleepTask ---
void GoToSleepTask(void* pvParameters) {
    for (;;) {
        // Czekaj na powiadomienie od ButtonTask
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) > 0) {
            Serial.println("[SLEEP TASK] Starting deep sleep sequence...");

            // Czekaj na zwolnienie przycisku (max 2s)
            unsigned long start = millis();
            while (digitalRead(WAKE_BUTTON_PIN) == LOW && millis() - start < 2000) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            if (digitalRead(WAKE_BUTTON_PIN) == LOW) {
                Serial.println("[SLEEP TASK] Button still pressed. Aborting sleep.");
                continue; // wróć do czekania na powiadomienie
            }

            Serial.println("[SLEEP TASK] Button released, waiting for debounce...");
            vTaskDelay(pdMS_TO_TICKS(500));

            // --- Wymuszenie flush SD i wysyłki TB ---
            sdForcedFlag = true;
            tbForcedFlag = true;
            if (sdModuleTaskHandle) xTaskNotifyGive(sdModuleTaskHandle);
            if (thingsboardTaskHandle) xTaskNotifyGive(thingsboardTaskHandle);

            // --- Czekaj na zakończenie obu operacji (max 5s na każde) ---
            if (sdModuleTaskHandle) {
                if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) == 0) {
                    Serial.println("[SLEEP TASK] Timeout waiting for SD flush.");
                } else {
                    Serial.println("[SLEEP TASK] SD flush completed.");
                }
            }

            if (thingsboardTaskHandle) {
                if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) == 0) {
                    Serial.println("[SLEEP TASK] Timeout waiting for TB send.");
                } else {
                    Serial.println("[SLEEP TASK] TB send completed.");
                }
            }

            sdModule.softClose(); //--- Zamknięcie SD przed snem ---

            // --- Ustawienie wakeup i deep sleep ---
            esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BUTTON_PIN, 0);
            Serial.println("[SLEEP TASK] Going to sleep.");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_deep_sleep_start();
        }
    }
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
    pinMode(LED_PIN_1, OUTPUT);
    pinMode(LED_PIN_2, OUTPUT);
    pinMode(LED_PIN_3, OUTPUT);
    attachInterrupt(digitalPinToInterrupt(WAKE_BUTTON_PIN), handleWakeISR, FALLING);

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

    // Register RPC callback
    tbClient.setRpcCallback(rpcForceCallback);

    gpsInit(GPS_BAUDRATE, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("[GPS] Initialized");

    tempSensor.begin();
    Serial.println("[TEMP] DS18B20 initialized");

    TimeManager::begin(PPS_PIN);
    TimeManager::enableNtpBackup("pool.ntp.org", "time.google.com", "time.cloudflare.com");

    xTaskCreate(CoordinatorTask, "Coordinator", 8192, NULL, 2, &coordinatorTaskHandle);
    xTaskCreate(TaskGPS,  "GPSTask",  8192, NULL, 1,  &gpsModuleTaskHandle);
    xTaskCreate(TaskTemp, "TempTask", 8192, NULL, 1,  &tempModuleTaskHandle);
    xTaskCreate(TaskSD,   "SDTask",   16384, NULL, 1,  &sdModuleTaskHandle);
    xTaskCreate(TaskTB,   "TBTask",   16384, NULL, 1,  &thingsboardTaskHandle);
    xTaskCreate(ButtonTask, "ButtonTask", 4096, NULL, 1, &buttonTaskHandle);
    xTaskCreate(GoToSleepTask, "GoToSleep", 4096, NULL, 2, &goToSleepTaskHandle);

    startWebServer(80);
    Serial.println("[WEB] Server started");
}



void loop() {

}


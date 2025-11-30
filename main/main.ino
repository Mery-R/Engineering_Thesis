#include <WiFi.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "SensorData.h"
#include "RingBuffer.h"
#include "ThingsBoardClient.h"
#include "SdModule.h"
#include "WiFiManager.h"
#include "WebServerModule.h"
#include "TimeManager.h"
#include "GpsModule.h"
#include "esp_sleep.h"

SET_TIME_BEFORE_STARTING_SKETCH_MS(5000);

// -----------------------------------------------------
// ---------------- WiFi & ThingsBoard -----------------
// -----------------------------------------------------

// WiFi settings
// {SSID, Password}
std::vector<WiFiConfig> WIFI_CONFIG = {
    {"KTO-Rosomak", "12345678"},
    {"Dom2", "xyz"},
    {"Hotspot", "..." }
};

// ThingsBoard settings
const char* THINGSBOARD_SERVER = "demo.thingsboard.io"; // ThingsBoard server address (demo.thingsboard.io for demo server)
const int THINGSBOARD_PORT = 1883;                      // ThingsBoard server port (1883 for MQTT)
const char* MQTT_CLIENT_ID = "esp32_test";              // Client ID for ThingsBoard
const char* MQTT_USERNAME  = "user123";                 // Username for ThingsBoard
const char* MQTT_PASSWORD  = "8erz5sxd48lm797nr4ch";    // Password for ThingsBoard


// -----------------------------------------------------
// ------------------ GPIO Settings --------------------
// -----------------------------------------------------

// LED pinout
#define LED_WiFi 25
#define LED_GPS 26
#define LED_SD 27

// GPS pinout
#define PPS_PIN 32
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define GPS_BAUDRATE 115200

// SD card pinout
#define SD_CS_PIN 5

// DS18B20 pinout
#define ONE_WIRE_BUS 4

// Wake button pinout
#define WAKE_BUTTON_PIN 33

// -----------------------------------------------------
// -------------------- Settings -----------------------
// -----------------------------------------------------

// Delays 
#define Delay_MAIN 15000 // Delay in milliseconds for main loop
#define Delay_SD 15000 // Delay in milliseconds for SD card operations
#define Delay_WIFI 30000 // Delay in milliseconds for WiFi operations

// SD card settings
const unsigned long sdSendInterval = 100; // Time in ms between SD sends

// Ring buffer settings
const int RINGBUFFER_CAPACITY = 60; // Size of the ring buffer
const int BATCH_SIZE = 2; // Number of records that trigger SD write
const int MIN_BATCH_SIZE = 2; // Minimum number of records to send

// Time sync setting
const bool REQUIRE_VALID_TIME = true; // Set to true to wait for time sync before saving data



// -----------------------------------------------------
// ---------- DO NOT CHANGE BELOW THIS LINE ------------
// -----------------------------------------------------



// -----------------------------------------------------
// ------------------- Constants -----------------------
// -----------------------------------------------------

// Flags for forced sending
volatile bool sdForcedFlag = false;
volatile bool tbForcedFlag = false;
// Flags for sleep
volatile bool goToSleepRequested = false;
// Flags for button debouncing
unsigned long lastButtonPressTime = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 300;
// Flags for SD and TB sending
unsigned long lastSDSend = 0;
// Data structure
SensorData data;

// -----------------------------------------------------
// -------------------- Semaphores ---------------------
// -----------------------------------------------------

SemaphoreHandle_t dataSem;
SemaphoreHandle_t wifiEventSem = NULL;
SemaphoreHandle_t sdSignal = NULL;

// -----------------------------------------------------
// -------------------- Task Handlers ------------------
// -----------------------------------------------------

TaskHandle_t coordinatorTaskHandle = NULL;
TaskHandle_t buttonTaskHandle = NULL;
TaskHandle_t goToSleepTaskHandle = NULL;
TaskHandle_t tempModuleTaskHandle = NULL;
TaskHandle_t gpsModuleTaskHandle = NULL;
// TaskHandle_t sdModuleTaskHandle = NULL; // Removed
TaskHandle_t thingsboardTaskHandle = NULL;
TaskHandle_t wifiTaskHandle = NULL;

// -----------------------------------------------------
// -------------------- Modules ------------------------
// -----------------------------------------------------

SdModule sdModule(SD_CS_PIN);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);
WiFiManager wifiManager(WIFI_CONFIG);
RingBuffer<SensorData> ringBuffer(RINGBUFFER_CAPACITY);
GpsModule gpsModule(GPS_RX_PIN, GPS_TX_PIN, GPS_BAUDRATE);
ThingsBoardClient tbClient(THINGSBOARD_SERVER, THINGSBOARD_PORT, MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);

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
        if (!TimeManager::isSynchronized() && REQUIRE_VALID_TIME) {
            Serial.println("[COORD] Waiting for time sync...");
            if (wifiTaskHandle) xTaskNotifyGive(wifiTaskHandle);
            vTaskDelay(pdMS_TO_TICKS(Delay_MAIN));
            continue;
        }
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

        // Check if time is synchronized before storing data
        if (!TimeManager::isSynchronized()) {
            Serial.println("[COORD] Waiting for time sync... Data not saved.");
        } else {
            if (!ringBuffer.push(snapshot)) {
                Serial.println("[COORD] RingBuffer full, dropping snapshot");
            }

            // Notify TB task to process data
            if (thingsboardTaskHandle) {
                xTaskNotifyGive(thingsboardTaskHandle);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(Delay_MAIN));
    }
}

// --- TASK: WiFi Watchdog ---
void TaskWiFi(void* pvParameters) {
    for (;;) {
        // Wait for notification (from EventHandler) or timeout (30s periodic check)
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(Delay_WIFI));

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] WiFi lost/disconnected. Attempting reconnect...");
            try {
                wifiManager.connectToBest();
            } catch (...) {
                Serial.println("[WiFi] Exception during reconnect");
            }

            // If still disconnected, wait a bit to avoid rapid looping (debounce)
            if (WiFi.status() != WL_CONNECTED) {
                 vTaskDelay(pdMS_TO_TICKS(Delay_WIFI)); 
                 // Clear any notifications received during this backoff to avoid immediate re-trigger
                 ulTaskNotifyTake(pdTRUE, 0);
            }
        }
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
            // Notify WiFi Task to handle connection (prevents double scan)
            if (wifiTaskHandle) xTaskNotifyGive(wifiTaskHandle);
            break;
        
        // Station connected
        case WIFI_EVENT_STA_CONNECTED:
            digitalWrite(LED_WiFi, HIGH);
            break;

        // Station disconnected
        case WIFI_EVENT_STA_DISCONNECTED:
            digitalWrite(LED_WiFi, LOW);
            // Notify WiFi Task to handle reconnection
            if (wifiTaskHandle) xTaskNotifyGive(wifiTaskHandle);
            break;

        }
    }

    // Print IP address when got IP
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = (ip_event_got_ip_t*)data;
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());

    }
}


// --- TASK: GPS ---
void TaskGPS(void* pvParameters){
    for(;;){
        // wait until coordinator notifies this task
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) > 0) {
            gpsModule.wake();
            bool gpsOk = false;
            unsigned long start = millis();

            while(millis()-start < 5000) {
              gpsOk = gpsModule.process() || gpsOk;
              vTaskDelay(5);
            }

            uint64_t now = TimeManager::getTimestampMs();

            xSemaphoreTake(dataSem, portMAX_DELAY);
            if(gpsOk && gpsModule.hasFix()){
                TimeManager::updateFromGps(gpsModule.getUnixTime());
                GpsDataPacket packet = gpsModule.getData();
                data.lat = packet.lat;
                data.lon = packet.lon;
                data.elevation = packet.elevation;
                data.speed = packet.speed;
                data.last_gps_fix_timestamp = now;
                data.error_code &= ~ERR_GPS_NO_FIX;
                digitalWrite(LED_GPS, HIGH);
            } else {
                data.error_code |= ERR_GPS_NO_FIX;
                digitalWrite(LED_GPS, LOW);
            }
            
            // Log status using the module's method
            gpsModule.logStatus(gpsOk);

            xSemaphoreGive(dataSem);
            
            gpsModule.sleep(); // Put GPS to sleep after reading

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
            if(tempC>-55 && tempC<125){
                data.temp = tempC; 
                data.last_temp_read_timestamp = now;
                data.error_code &= ~ERR_TEMP_FAIL;
                Serial.println("[TEMP] Temp: " + String(tempC));
            } else {
                data.error_code |= ERR_TEMP_FAIL;
                Serial.println("[TEMP] Error");
            }
            xSemaphoreGive(dataSem);

            if (coordinatorTaskHandle) xTaskNotifyGive(coordinatorTaskHandle);
        }
    }
}

// --- TASK: ThingsBoard (and SD Logging) ---
void TaskTB(void* pvParameters){
    SensorData batch[BATCH_SIZE];
    
    for(;;){
        bool forced = false;

        // Wait for notification OR timeout (1s) to check for offline data
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

        if (tbForcedFlag) {
            forced = true;
            tbForcedFlag = false;
        }

        // 1. Process RingBuffer (RAM)
        // We process as long as we have data or if forced
        while (ringBuffer.size() >= MIN_BATCH_SIZE) {
            int count = ringBuffer.popBatch(batch, BATCH_SIZE); // Pop up to BATCH_SIZE
            if (count == 0) break;

            bool sent = false;

            // Try to send via MQTT if connected
            if (WiFi.status() == WL_CONNECTED) {
                if (!tbClient.isConnected()) tbClient.connect();
                
                if (tbClient.isConnected()) {
                    JsonDocument doc;
                    JsonArray arr = doc.to<JsonArray>();
                    for (int i = 0; i < count; ++i) {
                        JsonObject o = arr.createNestedObject();
                        sensorDataToJson(batch[i], o);
                    }
                    if (tbClient.sendBatchDirect(arr)) {
                        sent = true;
                        Serial.printf("[TB] Sent %d records directly from buffer\n", count);
                    }
                }
            }

            // Update tb_sent flag in batch
            for (int i = 0; i < count; ++i) {
                batch[i].tb_sent = sent;
            }

            // Always save to Archive
            if (sdModule.checkAndRemount()){
                // Save to Archive always
                sdModule.logToArchive(batch, count);
                // If send failed, save also to Pending
                if (!sent) {
                    sdModule.logToPending(batch, count);
                }
            } else {
                Serial.println("[TB] SD not ready, cannot archive/pending!");
            }
        }

        // 2. Idle State Handling: Process Pending File
        // Only if RingBuffer is empty AND WiFi is connected
        if (ringBuffer.size() == 0 && WiFi.status() == WL_CONNECTED) {
            if (!tbClient.isConnected()) tbClient.connect();

            if (tbClient.isConnected()) {
                // Check if pending file exists/has data
                // We read in chunks.
                size_t offset = 0;
                bool allSent = true;
                bool hasPending = false;

                while (true) {
                    JsonDocument doc;
                    JsonArray arr = doc.to<JsonArray>();
                    
                    // Read a batch from pending
                    int readCount = sdModule.readPendingBatch(arr, BATCH_SIZE, offset);
                    
                    if (readCount == 0) {
                        // End of file or empty
                        break;
                    }
                    
                    hasPending = true;

                    // Try to send
                    if (!tbClient.sendBatchDirect(arr)) {
                        allSent = false;
                        Serial.printf("[TB] FAILED to send %d records from Pending\n", readCount);
                        break;
                    }
                    Serial.printf("[TB] SUCCESS: Sent %d records from Pending\n", readCount);
                    
                    // If sent, continue to next batch
                    vTaskDelay(2000); // Small delay to yield
                }

                // If we had pending data and ALL were sent successfully, clear the file
                if (hasPending && allSent) {
                    sdModule.clearPending();
                }
            }
        }

        // Sleep task acknowledgment
        if (goToSleepRequested && forced) {
            goToSleepRequested = false;
            xTaskNotifyGive(goToSleepTaskHandle);
        }
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
            goToSleepRequested = true; // Set global flag so tasks know to notify back
            
            // Notify TB Task (which now handles SD as well)
            if (thingsboardTaskHandle) xTaskNotifyGive(thingsboardTaskHandle);

            // --- Czekaj na zakończenie operacji (max 5s) ---
            if (thingsboardTaskHandle) {
                if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) == 0) {
                    Serial.println("[SLEEP TASK] Timeout waiting for TB/SD flush.");
                } else {
                    Serial.println("[SLEEP TASK] TB/SD flush completed.");
                }
            }

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

    pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_WiFi, OUTPUT);
    pinMode(LED_GPS, OUTPUT);
    pinMode(LED_SD, OUTPUT);
    attachInterrupt(digitalPinToInterrupt(WAKE_BUTTON_PIN), handleWakeISR, FALLING);

    sdSignal = xSemaphoreCreateBinary();
    wifiEventSem = xSemaphoreCreateBinary();
    dataSem = xSemaphoreCreateMutex();

    if (sdModule.begin()){
        digitalWrite(LED_SD, HIGH);
    }
    else{
        digitalWrite(LED_SD, LOW);
    }

    wifiManager.begin();

    tbClient.setRpcCallback(rpcForceCallback);

    gpsModule.begin();

    tempSensor.begin();
    Serial.println("[TEMP] DS18B20 initialized");

    TimeManager::begin(PPS_PIN);
    TimeManager::enableNtpBackup("pool.ntp.org", "time.google.com", "time.cloudflare.com");

    startWebServer(80);
    Serial.println("[WEB] Server started");

    xTaskCreate(CoordinatorTask, "Coordinator", 8192, NULL, 2, &coordinatorTaskHandle);
    xTaskCreate(TaskGPS,  "GPSTask",  8192, NULL, 1,  &gpsModuleTaskHandle);
    xTaskCreate(TaskTemp, "TempTask", 8192, NULL, 1,  &tempModuleTaskHandle);
    // xTaskCreate(TaskSD,   "SDTask",   16384, NULL, 1,  &sdModuleTaskHandle); // Removed
    xTaskCreate(TaskTB,   "TBTask",   16384, NULL, 1,  &thingsboardTaskHandle);
    xTaskCreate(TaskWiFi, "WiFiTask", 4096, NULL, 1, &wifiTaskHandle);
    xTaskCreate(ButtonTask, "ButtonTask", 4096, NULL, 1, &buttonTaskHandle);
    xTaskCreate(GoToSleepTask, "GoToSleep", 4096, NULL, 2, &goToSleepTaskHandle);

    // Kickstart WiFi task to connect immediately
    if (wifiTaskHandle) xTaskNotifyGive(wifiTaskHandle);

    // Delete the default Arduino loopTask as we use FreeRTOS tasks
    vTaskDelete(NULL);
}

void loop() {

}
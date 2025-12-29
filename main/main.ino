// --- Libraries ---
#include <WiFi.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <esp_task_wdt.h>
#include "SensorData.h"
#include "esp_sleep.h"

// --- Modules ---
#include "ThingsBoardClient.h"
#include "SdModule.h"
#include "WiFiManager.h"
#include "WebServerModule.h"
#include "TimeManager.h"
#include "GpsModule.h"
#include "CanModule.h"

SET_TIME_BEFORE_STARTING_SKETCH_MS(5000); // Set time before starting sketch (ms)

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

// LED (Red = 25, Green = 26, Blue = 27)
#define LED_WiFi 27
#define LED_GPS 26
#define LED_SD 25 

// GPS
#define PPS_PIN 32
#define GPS_RX_PIN 17
#define GPS_TX_PIN 16
#define GPS_BAUDRATE 115200

// SD (MOSI = 23, MISO = 19, SCK = 18, CS = 5)
#define SD_CS_PIN 5

// DS18B20
#define ONE_WIRE_BUS 4

// Wake button
#define WAKE_BUTTON_PIN 33

// CAN
#define CAN_RX_PIN 21
#define CAN_TX_PIN 22

// -----------------------------------------------------
// -------------------- Settings -----------------------
// -----------------------------------------------------

// WDT settings
#define WDT_TIMEOUT 60                      // Time before WDT reset (seconds)

// Delays (volatile for dynamic update)
volatile int Delay_MAIN = 15000;            // Main loop delay (ms) (can be changed via ThingsBoard)
volatile int Delay_WIFI = 30000;            // WiFi loop delay (ms) (can be changed via ThingsBoard)
volatile int ATTR_REQUEST_INTERVAL = 300000;// Attributes request interval (ms) (can be changed via ThingsBoard)
volatile int MQTT_KEEPALIVE_TIMEOUT = 2000; // MQTT keep-alive timeout (ms) (can be changed via ThingsBoard)

// Buffer settings (volatile for dynamic update)
volatile int BUFFER_CAPACITY = 60;          // Buffer capacity (can be changed via ThingsBoard)
volatile int BATCH_SIZE = 2;                // Batch size (can be changed via ThingsBoard)
volatile int MIN_BATCH_SIZE = 2;            // Minimum batch size (can be changed via ThingsBoard)
#define MAX_BATCH_SIZE 20                   // Maximum batch size (hard limit)

// Time sync setting (volatile for dynamic update)
volatile bool REQUIRE_VALID_TIME = true;    // Time sync setting (can be changed via ThingsBoard)

// Sensor Enable Flags
bool ENABLE_GPS = true;
bool ENABLE_TEMP = true;
bool ENABLE_CAN = false;    



// -----------------------------------------------------
// ---------- DO NOT CHANGE BELOW THIS LINE ------------
// -----------------------------------------------------



// -----------------------------------------------------
// ------------------- Constants -----------------------
// -----------------------------------------------------

// Event Bits
#define EVENT_GPS_READY  (1 << 0)
#define EVENT_TEMP_READY (1 << 1)
#define EVENT_CAN_READY  (1 << 2)
// Flags for button debouncing
const unsigned long BUTTON_DEBOUNCE_MS = 300;
// Sleep request flag
volatile bool sleepRequestActive = false;
// Delay
const unsigned int TIMEOUT = Delay_MAIN - 3000;
// Data structure
SensorData data;
QueueHandle_t dataQueue;

// -----------------------------------------------------
// -------------------- Semaphores ---------------------
// -----------------------------------------------------
// Used to synchronize access to shared resources

SemaphoreHandle_t dataSem = NULL;
SemaphoreHandle_t sdMutex = NULL;
EventGroupHandle_t sensorEventGroup = NULL;

// -----------------------------------------------------
// -------------------- Task Handlers ------------------
// -----------------------------------------------------
// Used to synchronize task execution

TaskHandle_t coordinatorTaskHandle = NULL;
TaskHandle_t sleepTaskHandle = NULL;
TaskHandle_t tempModuleTaskHandle = NULL;
TaskHandle_t gpsModuleTaskHandle = NULL;
TaskHandle_t dataSyncTaskHandle = NULL;
TaskHandle_t wifiTaskHandle = NULL;
TaskHandle_t canModuleTaskHandle = NULL;
TaskHandle_t webServerTaskHandle = NULL;

// -----------------------------------------------------
// -------------------- Modules ------------------------
// -----------------------------------------------------
// Used to manage and control external modules

SdModule sdModule(SD_CS_PIN);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);
WiFiManager wifiManager(WIFI_CONFIG);
CanModule canModule(CAN_RX_PIN, CAN_TX_PIN);
GpsModule gpsModule(GPS_RX_PIN, GPS_TX_PIN, GPS_BAUDRATE);
ThingsBoardClient tbClient(THINGSBOARD_SERVER, THINGSBOARD_PORT, MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);

// -----------------------------------------------------
// -------------------- Functions ----------------------
// -----------------------------------------------------

void IRAM_ATTR isrButton() {
    // Notify TaskSleep instead of the old ButtonTask
    if (sleepTaskHandle) {
        vTaskNotifyGiveFromISR(sleepTaskHandle, NULL);
    }
}

void initWatchdog(int timeoutSeconds) {
    // Attempt to deinit first if already initialized by Arduino Core
    esp_task_wdt_deinit();
    
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = (uint32_t)timeoutSeconds * 1000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true
    };
    
    if (esp_task_wdt_init(&wdt_config) != ESP_OK) {
        Serial.println("[SETUP] WDT Init failed!");
    } else {
        Serial.printf("[SETUP] WDT Initialized (%d s)\n", timeoutSeconds);
    }
}

// Callbacks
void attributesCallback(const JsonObject &data) {
    if (data.containsKey("Delay_MAIN")) {
        Delay_MAIN = data["Delay_MAIN"];
        Serial.printf("Updated Delay_MAIN: %d\n", Delay_MAIN);
    }
    if (data.containsKey("Delay_WIFI")) {
        Delay_WIFI = data["Delay_WIFI"];
        Serial.printf("Updated Delay_WIFI: %d\n", Delay_WIFI);
    }
    if (data.containsKey("BATCH_SIZE")) {
        int val = data["BATCH_SIZE"];
        if (val > MAX_BATCH_SIZE) val = MAX_BATCH_SIZE;
        BATCH_SIZE = val;
        Serial.printf("Updated BATCH_SIZE: %d\n", BATCH_SIZE);
    }
    if (data.containsKey("MIN_BATCH_SIZE")) {
        MIN_BATCH_SIZE = data["MIN_BATCH_SIZE"];
        Serial.printf("Updated MIN_BATCH_SIZE: %d\n", MIN_BATCH_SIZE);
    }
    if (data.containsKey("BUFFER_CAPACITY")) {
        BUFFER_CAPACITY = data["BUFFER_CAPACITY"];
        Serial.printf("Updated BUFFER_CAPACITY (needs restart): %d\n", BUFFER_CAPACITY);
    }
    if (data.containsKey("REQUIRE_VALID_TIME")) {
        REQUIRE_VALID_TIME = data["REQUIRE_VALID_TIME"];
        Serial.printf("Updated REQUIRE_VALID_TIME: %d\n", (int)REQUIRE_VALID_TIME);
    }
}

// -----------------------------------------------------
// ----------------------- TASKS -----------------------
// -----------------------------------------------------

// Coordinator: prepares timestamp, triggers sensor reads, waits for completion and pushes snapshot to buffer
void CoordinatorTask(void* pvParameters) {
    // Add to WDT
    esp_task_wdt_add(NULL);

    // Get last wake time
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    for (;;) {
        // Wait for next wake
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(Delay_MAIN));
        esp_task_wdt_reset();

        // Print alive for debug
        Serial.println("[COORD] Wakeup");

        // Trigger tasks via notifications (only enabled ones)
        if (ENABLE_GPS && gpsModuleTaskHandle) xTaskNotifyGive(gpsModuleTaskHandle);
        if (ENABLE_TEMP && tempModuleTaskHandle) xTaskNotifyGive(tempModuleTaskHandle);
        if (ENABLE_CAN && canModuleTaskHandle) xTaskNotifyGive(canModuleTaskHandle);

        // Determine which events to wait for
        EventBits_t expectedBits = 0;
        if (ENABLE_GPS) expectedBits |= EVENT_GPS_READY;
        if (ENABLE_TEMP) expectedBits |= EVENT_TEMP_READY;
        if (ENABLE_CAN) expectedBits |= EVENT_CAN_READY;

        // Wait for all enabled sensors (flags) with timeout
        EventBits_t uxBits = 0;
        if (expectedBits > 0) {
            uxBits = xEventGroupWaitBits(
                sensorEventGroup,
                expectedBits,
                pdTRUE,        // Clear bits on exit
                pdTRUE,        // Wait for ALL bits (AND)
                pdMS_TO_TICKS(TIMEOUT)
            );
        }

        // Check which ones responded (only if enabled)
        bool gpsDone  = !ENABLE_GPS || (uxBits & EVENT_GPS_READY);
        bool tempDone = !ENABLE_TEMP || (uxBits & EVENT_TEMP_READY);
        bool canDone  = !ENABLE_CAN || (uxBits & EVENT_CAN_READY);

        // Output missing sensors
        if (!gpsDone || !tempDone || !canDone) {
             Serial.printf("[COORD] Timeout! Missing: %s%s%s\n", 
                gpsDone ? "" : "GPS ", 
                tempDone ? "" : "TEMP ",
                canDone ? "" : "CAN");
        }

        xSemaphoreTake(dataSem, portMAX_DELAY);
        // Get timestamp
        data.timestamp = TimeManager::getTimestampMs();
        data.timestamp_time_source = TimeManager::getTimeSource();

        // Make a snapshot
        SensorData snapshot = data;
        xSemaphoreGive(dataSem);

        TimeManager::updateFromGps(gpsModule.getUnixTime());

        // Check if time is synchronized before storing data
        if (!TimeManager::isSynchronized() && REQUIRE_VALID_TIME) {
            Serial.println("[COORD] Waiting for time sync...");
            if (wifiTaskHandle) xTaskNotifyGive(wifiTaskHandle);
            continue;
        } else {
            if (xQueueSend(dataQueue, &snapshot, 0) != pdTRUE) {
                Serial.println("[COORD] Queue full, dropping snapshot");
            }
            // Notify TB task to process data
            if (dataSyncTaskHandle) xTaskNotifyGive(dataSyncTaskHandle);
        }
    }
}

// --- TASK: WiFi ---
void TaskWiFi(void* pvParameters) {
    // Add to WDT
    esp_task_wdt_add(NULL);

    tbClient.setAttributesCallback(attributesCallback);
    
    for (;;) {
        // Wait for notification or timeout
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(Delay_WIFI));
        esp_task_wdt_reset();

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] WiFi lost/disconnected. Attempting reconnect...");

            if (wifiManager.connectToBest()) {
                 // Clear any notifications received
                 ulTaskNotifyTake(pdTRUE, 0);
            }
            else {
                Serial.println("[WiFi] Failed to reconnect.");
            }
        }
    }
}

// WifiEventHandler: handles WiFi events
void WiFiEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    if (base == WIFI_EVENT) {
        switch (id) {

        // Station started
        case WIFI_EVENT_STA_START:
            // Notify WiFi Task to handle connection (prevents double scan)
            if (wifiTaskHandle) xTaskNotifyGive(wifiTaskHandle);
            break;
        
        // Station connected
        case WIFI_EVENT_STA_CONNECTED:
            break;

        // Station disconnected
        case WIFI_EVENT_STA_DISCONNECTED:
            digitalWrite(LED_WiFi, LOW);
            // Notify WiFi Task to handle reconnection
            //if (wifiTaskHandle) xTaskNotifyGive(wifiTaskHandle);
            break;

        }
    }

    // Print IP address when got IP
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = (ip_event_got_ip_t*)data;
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        digitalWrite(LED_WiFi, HIGH);

        if (webServerTaskHandle) xTaskNotifyGive(webServerTaskHandle); // Notify WebServer Task
    }
}

// --- TASK: WebServer ---
void TaskWebServer(void* pvParameters) {

    startWebServer(80); // Start server
    
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        Serial.println("[WebServer] Starting server loop.");
        
        // Add to WDT
        esp_task_wdt_add(NULL);

        // Loop while connected
        while (WiFi.status() == WL_CONNECTED) {
            esp_task_wdt_reset(); // Reset WDT
            server.handleClient(); // Handle client
            vTaskDelay(pdMS_TO_TICKS(100)); // Refresh every 100ms
        }
        Serial.println("[WebServer] WiFi lost. Stopping server loop.");
        
        // Remove from WDT
        esp_task_wdt_delete(NULL);
    }
}


// --- TASK: GPS ---
void TaskGPS(void* pvParameters){

    esp_task_wdt_add(NULL); // Add to WDT
    
    for(;;) {
        // Wait for notification
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        unsigned long start = millis();
        bool hasFix = false;

        // Read GPS data for max TIMEOUT
        do {
            esp_task_wdt_reset(); // Feed the watchdog

            // Process all available GPS data
            while (gpsModule.available()) gpsModule.process(); 

            // If fix is acquired, break early
            if (gpsModule.hasFix()) {
                hasFix = true;
                break; 
            }
            
            // Short processor breath
            vTaskDelay(pdMS_TO_TICKS(10));

        } while (millis() - start < TIMEOUT);

        // Write results
        xSemaphoreTake(dataSem, portMAX_DELAY);
        
        if (hasFix) {
            // Copy data
            GpsDataPacket packet = gpsModule.getData();
            data.lat = packet.lat;
            data.lon = packet.lon;
            data.alt = packet.alt;
            data.speed = packet.speed;
            data.last_gps_fix_timestamp = TimeManager::getTimestampMs();
            
            data.error_code &= ~ERR_GPS_NO_FIX;
            Serial.printf("[GPS] Fix acquired! Lat: %f, Lon: %f\n", data.lat, data.lon);
            digitalWrite(LED_GPS, HIGH);
        } else {
            data.error_code |= ERR_GPS_NO_FIX;
            Serial.printf("[GPS] Timeout: No fix within %lu ms.\n", TIMEOUT);
            digitalWrite(LED_GPS, LOW);
        }
        
        xSemaphoreGive(dataSem);

        // Notify coordinator that GPS finished
        xEventGroupSetBits(sensorEventGroup, EVENT_GPS_READY);
    }
}

// --- TASK: Temperature ---
void TaskTemp(void* pvParameters) {
   
    esp_task_wdt_add(NULL);  // Add to WDT
    
    const TickType_t CONVERSION_DELAY = pdMS_TO_TICKS(750); // Conversion delay for 12-bit resolution is max 750ms

    for (;;) {
        // Wait for signal from Coordinator
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        esp_task_wdt_reset();

        // Request temperature
        tempSensor.requestTemperatures();
        
        // Asynchronous waiting (Yield)
        vTaskDelay(CONVERSION_DELAY);

        // Read result
        float tempC = tempSensor.getTempCByIndex(0);
        
        // Write data
        xSemaphoreTake(dataSem, portMAX_DELAY);
        
        // DEVICE_DISCONNECTED_C is constant (-127.0)
        if (tempC != DEVICE_DISCONNECTED_C && tempC > -55 && tempC < 125) {
            data.temp = tempC; 
            data.last_temp_read_timestamp = TimeManager::getTimestampMs();
            data.error_code &= ~ERR_TEMP_FAIL;
            
            Serial.printf("[TEMP] Temp: %.2f C\n", tempC);
        } else {
            data.error_code |= ERR_TEMP_FAIL;
            Serial.println("[TEMP] Error: Read failed");
        }
        
        xSemaphoreGive(dataSem);

        // Notify Coordinator that temperature is ready
        xEventGroupSetBits(sensorEventGroup, EVENT_TEMP_READY);
    }
}

// --- TASK: CAN ---
void TaskCAN(void* pvParameters) {
    // Add to WDT
    esp_task_wdt_add(NULL);

    twai_message_t msg;

    for (;;) {
        // Reset WDT
        esp_task_wdt_reset();

        // Get message
        if (canModule.getMessage(msg)) {
            
            // Scale speed
            float speed = canModule.scaleSpeed(msg);
            
            // If valid speed
            if (speed >= 0) {
                
                // Write data
                xSemaphoreTake(dataSem, portMAX_DELAY);
                data.can_speed = speed;
                data.last_can_read_timestamp = TimeManager::getTimestampMs();
                xSemaphoreGive(dataSem);

                // Signal to Coordinator
                xEventGroupSetBits(sensorEventGroup, EVENT_CAN_READY);
            }
        } 
        else {
            // No message - short pause
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// --- TASK: ThingsBoard & SD Logging ---
void TaskDataSync(void* pvParameters) {
    // Register Watchdog
    esp_task_wdt_add(NULL);
    
    SensorData batch[MAX_BATCH_SIZE];
    static unsigned long lastAttrRequest = 0;

    for (;;) {
        // Wait for notification or timeout
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MQTT_KEEPALIVE_TIMEOUT));
        
        esp_task_wdt_reset();

        // --- MQTT Maintenance ---
        if (WiFi.status() == WL_CONNECTED) {
            if (!tbClient.isConnected()) {

                tbClient.connect();

                esp_task_wdt_reset();
            }
            
            if (tbClient.isConnected()) {
                tbClient.loop(); // maintain connection
                esp_task_wdt_reset();   
                
                // Request attributes
                if (millis() - lastAttrRequest > ATTR_REQUEST_INTERVAL) {
                    lastAttrRequest = millis();
                    tbClient.requestSharedAttributes();
                }
            }
        }

        // --- Process New Data (RAM Queue) ---
        // Flush the RAM buffer to prevent overflow
        while (uxQueueMessagesWaiting(dataQueue) >= MIN_BATCH_SIZE) {
            esp_task_wdt_reset();

            // Pop data from Queue
            int count = 0;
            // Limit batch size to either BATCH_SIZE or MAX available
            int limit = (BATCH_SIZE < MAX_BATCH_SIZE) ? BATCH_SIZE : MAX_BATCH_SIZE;

            while (count < limit && uxQueueMessagesWaiting(dataQueue) > 0) {
                if (xQueueReceive(dataQueue, &batch[count], 0) == pdTRUE) {
                    count++;
                }
                else {
                    break;
                }
            }

            if (count == 0) break;

            // Try to send via MQTT
            bool sent = false;
            if (tbClient.isConnected()) {
                JsonDocument doc;
                JsonArray arr = doc.to<JsonArray>();
                for (int i = 0; i < count; ++i) {
                    JsonObject o = arr.createNestedObject();
                    sensorDataToJson(batch[i], o);
                }
                
                if (tbClient.sendBatchDirect(arr)) {
                    sent = true;
                    Serial.printf("[TB] Sent %d records from Buffer.\n", count);
                }
            }

            // Update sent status in the struct
            for (int i = 0; i < count; ++i) batch[i].tb_sent = sent;

            // Save to SD Card
            if (sdModule.ensureReady()) {
                digitalWrite(LED_SD, HIGH);
                
                // Always save to Archive
                sdModule.logToArchive(batch, count);
                
                // If offline, save to Pending as well
                if (!sent) {
                    sdModule.logToPending(batch, count);
                }
            } else {
                digitalWrite(LED_SD, LOW);
                Serial.println("[SD] SD Error: Cannot save data.");
            }
        }

        // --- Process Old Data (Pending File) ---
        // Only if online AND RAM queue is empty
        if (tbClient.isConnected() && uxQueueMessagesWaiting(dataQueue) == 0) {
        
            while (uxQueueMessagesWaiting(dataQueue) < MIN_BATCH_SIZE) {
                esp_task_wdt_reset();

                JsonDocument doc;
                JsonArray arr = doc.to<JsonArray>();

                // Read from SD
                int readCount = sdModule.readPendingBatch(arr, BATCH_SIZE);

                if (readCount == 0) break; // No more pending data

                // Try to send
                if (tbClient.sendBatchDirect(arr)) {
                    Serial.printf("[TB] Sent %d records from Pending.\n", readCount);
                    
                    // Success: Clear the pending file
                    sdModule.removeFirstRecords(readCount); 
                } else {
                    // Failed to send, retry later
                    break; 
                }
                
                vTaskDelay(pdMS_TO_TICKS(2000)); // Yield to other tasks
            }
        }

        if (sleepRequestActive) {
            sleepRequestActive = false;
            if (sleepTaskHandle) xTaskNotifyGive(sleepTaskHandle);
        }
    }
}


// --- TASK: Sleep Manager ---
// Handles button press (ISR), debounce, waits for release, syncs data, and enters deep sleep.
void TaskSleep(void* pvParameters) {
    
    for (;;) {
        // Wait for notification from ISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        Serial.println("[SLEEP] Button ISR detected. Checking debounce...");

        // Simple Debounce
        // Wait and check if button is still pressed (LOW)
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
        
        if (digitalRead(WAKE_BUTTON_PIN) == HIGH) {
            Serial.println("[SLEEP] False alarm (noise).");
            continue; // Go back to waiting
        }

        Serial.println("[SLEEP] Button pressed. Waiting for release...");
        
        while (digitalRead(WAKE_BUTTON_PIN) == LOW) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        Serial.println("[SLEEP] Button released. Initiating sleep sequence...");

        //gpsModule.sleep();

        sleepRequestActive = true;

        // Notify data sync task to save data
        if (dataSyncTaskHandle) xTaskNotifyGive(dataSyncTaskHandle);

        // Wait for acknowledgment
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) > 0) {
            Serial.println("[SLEEP] Data sync completed.");
        } else {
            Serial.println("[SLEEP] Data sync timeout (forcing sleep anyway).");
        }

        // Enter Deep Sleep
        // Wake up when button pin goes LOW again
        esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BUTTON_PIN, 0);
        
        Serial.println("[SLEEP] Entering Deep Sleep. Goodbye!");
        Serial.flush(); // Ensure logs are printed before shutdown
        
        esp_deep_sleep_start();
    }
}

void setup() {
    Serial.begin(115200);
    delay(100); 
    Serial.println("\n[BOOT] System Starting...");

    // Watchdog
    initWatchdog(WDT_TIMEOUT);
    
    // GPIO
    pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_WiFi, OUTPUT);
    pinMode(LED_GPS, OUTPUT);
    pinMode(LED_SD, OUTPUT);

    // Resources
    dataSem = xSemaphoreCreateMutex();
    sdMutex = xSemaphoreCreateMutex();
    sensorEventGroup = xEventGroupCreate();

    dataQueue = xQueueCreate(BUFFER_CAPACITY, sizeof(SensorData));
    if (dataQueue == NULL) {
        Serial.println("[SETUP] CRITICAL ERROR: Failed to create dataQueue!");
        while(1);
    }

    // WiFi
    wifiManager.begin(); 
    xTaskCreate(TaskWiFi, "WiFi", 4096, NULL, 1, &wifiTaskHandle);

    // Time
    TimeManager::begin(PPS_PIN);

    // SD
    if (sdModule.ensureReady()){
        digitalWrite(LED_SD, HIGH);
        Serial.println("[SETUP] SD Card OK");
    } else {
        digitalWrite(LED_SD, LOW);
        Serial.println("[SETUP] SD Card Failed");
    }

    // GPS
    if (ENABLE_GPS) {
        Serial.println("[SETUP] Enabling GPS...");
        gpsModule.begin();
        xTaskCreate(TaskGPS, "GPS", 8192, NULL, 1, &gpsModuleTaskHandle);
        
        if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
             gpsModule.wake();
        }
    }

    // Temp
    if (ENABLE_TEMP) {
        Serial.println("[SETUP] Enabling Temp...");
        tempSensor.begin();
        tempSensor.setWaitForConversion(false); 
        xTaskCreate(TaskTemp, "Temp", 4096, NULL, 1, &tempModuleTaskHandle);
    }

    // CAN
    if (ENABLE_CAN) {
        Serial.println("[SETUP] Enabling CAN...");
        canModule.begin();
        xTaskCreate(TaskCAN, "CAN", 4096, NULL, 2, &canModuleTaskHandle);
    }

    // Logic Tasks
    xTaskCreate(CoordinatorTask, "SensorFusion", 8192, NULL, 2, &coordinatorTaskHandle);
    xTaskCreate(TaskDataSync, "Telemetry", 16384, NULL, 1, &dataSyncTaskHandle);
    xTaskCreate(TaskWebServer, "HttpServer", 4096, NULL, 1, &webServerTaskHandle);
    xTaskCreate(TaskSleep, "Sleep", 4096, NULL, 5, &sleepTaskHandle);

    // Interrupts
    attachInterrupt(digitalPinToInterrupt(WAKE_BUTTON_PIN), isrButton, FALLING);
    
    Serial.println("[SETUP] System Ready.");
    vTaskDelete(NULL);
}

void loop() {
    // Nothing to do here
}     
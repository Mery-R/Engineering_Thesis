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
RingBuffer<SensorData> ringBuffer(10);

const int LED_PIN_1 = 25;
const int LED_PIN_2 = 26;
const int LED_PIN_3 = 27;

#define WAKE_BUTTON_PIN 33

unsigned long lastActivity = 0;
const unsigned long SLEEP_AFTER_MS = 60000; // np. 1 minuta bezczynności

volatile bool shouldGoToSleep = false;
unsigned long lastButtonPressTime = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 300;

void IRAM_ATTR handleWakeButtonInterrupt() {
    // Debouncing: sprawdź czy od ostatniego naciśnięcia minęło dość czasu
    unsigned long currentTime = millis();
    if (currentTime - lastButtonPressTime > BUTTON_DEBOUNCE_MS) {
        shouldGoToSleep = true;
        lastButtonPressTime = currentTime;
        Serial.println("[BUTTON] Interrupt detected - will go to sleep");
    }
}

// --- TASK: GPS ---
void TaskGPS(void* pvParameters){
    for(;;){
        gpsWake();
        bool gpsOk = false;
        unsigned long start = millis();

        while(millis()-start < 5000) {
          gpsOk = gpsRead() || gpsOk;
          vTaskDelay(5);   // oddaj CPU
        }

        uint64_t now = TimeManager::getTimestampMs();

        xSemaphoreTake(dataSem, portMAX_DELAY);
        if(gpsOk && gpsHasFix()){
            TimeManager::updateFromGps(); // bez argumentów
            double lat, lon, elev, speed;
            getGpsData(lat, lon, elev, speed);
            data.lat = lat; 
            data.lon = lon; 
            data.elevation = 
            elev; data.speed = speed;
            data.timestamp = now; 
            data.timestamp_time_source = TIME_GPS;
            data.last_gps_fix_timestamp = now;
            data.error_code &= ~ERR_GPS_NO_FIX;
            digitalWrite(LED_PIN_2, HIGH);
        } else {
            data.error_code |= ERR_GPS_NO_FIX;
            data.timestamp = now;
            data.timestamp_time_source = (WiFi.status()==WL_CONNECTED)?TIME_WIFI:TIME_LOCAL;
            digitalWrite(LED_PIN_2, LOW);
        }
        SensorData snapshot = data;
        xSemaphoreGive(dataSem);

        ringBuffer.push(snapshot);
        vTaskDelay(pdMS_TO_TICKS(Delay_GPS));
    }
}

// --- TASK: Temperature ---
void TaskTemp(void* pvParameters){
    for(;;){
        tempSensor.requestTemperatures();
        float tempC = tempSensor.getTempCByIndex(0);
        uint64_t now = TimeManager::getTimestampMs();

        xSemaphoreTake(dataSem, portMAX_DELAY);
        if(tempC != DEVICE_DISCONNECTED_C && tempC>-55 && tempC<125){
            data.temp = tempC; data.last_temp_read_timestamp = now;
            data.timestamp = now;
            data.timestamp_time_source = (WiFi.status()==WL_CONNECTED && TimeManager::isSynchronized())?TIME_WIFI:TIME_LOCAL;
            data.error_code &= ~ERR_TEMP_FAIL;
        } else {
            data.error_code |= ERR_TEMP_FAIL;
            data.timestamp = now;
            data.timestamp_time_source = TIME_LOCAL;
        }
        SensorData snapshot = data;
        xSemaphoreGive(dataSem);

        ringBuffer.push(snapshot);
        vTaskDelay(pdMS_TO_TICKS(Delay_Temp));
    }
}

// --- TASK: SD Logging ---
void TaskSD(void* pvParameters){
    SensorData batch[10];
    for(;;){
        int count = ringBuffer.popBatch(batch, 10);
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

            if(!sdModule.appendRecords(arr)){
                Serial.println("[SD][ERR] Append failed!");
                xSemaphoreTake(dataSem, portMAX_DELAY);
                data.error_code |= ERR_SD_FAIL;
                xSemaphoreGive(dataSem);
            } 
        }
        vTaskDelay(pdMS_TO_TICKS(Delay_SD));
    }
}

// --- TASK: ThingsBoard ---
void TaskTB(void* pvParameters){
    for(;;){
        if(WiFi.status()==WL_CONNECTED && tbClient.isConnected()){
            digitalWrite(LED_PIN_3, HIGH);
            int sent = tbClient.sendBatchToTB(sdModule, 20); // wysyła batch z SD, oznacza rekordy jako tb_sent
        } else if(WiFi.status()==WL_CONNECTED){
            digitalWrite(LED_PIN_3, LOW);
            tbClient.connect();
        }
        vTaskDelay(pdMS_TO_TICKS(Delay_TB));
    }
}

// --- Task wołający logikę z WiFiController ---
void WiFiTask(void* pvParameters) {
    for (;;) {
        if (!wifiManager.isConnected()) {
            Serial.println("[WiFiTask] Not connected, attempting to reconnect...");
            if (wifiManager.connectToBest()) {
                Serial.println("[WiFiTask] Reconnected to WiFi");
                digitalWrite(LED_PIN_1, HIGH);
            } else {
                digitalWrite(LED_PIN_1, LOW);
                Serial.println("[WiFiTask] Reconnection failed, will retry later");
            }
        }
      vTaskDelay(pdMS_TO_TICKS(Delay_TB));
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
    attachInterrupt(digitalPinToInterrupt(WAKE_BUTTON_PIN), handleWakeButtonInterrupt, FALLING);

    dataSem = xSemaphoreCreateMutex();

    sdModule.begin();

    wifiManager.begin();

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

    startWebServer(80);
    Serial.println("[WEB] Server started");
}



void loop() {
    
    TimeManager::periodicCheck();

    uint64_t now = TimeManager::getTimestampMs();
    Serial.printf("Current timestamp: %llu\n", now);

    TimeManager::periodicCheck();

    // Jeśli przycisk został wciśnięty, przejdź w głęboki sen
    if (shouldGoToSleep) {
        shouldGoToSleep = false;
        goToDeepSleep();
    }

    // Alternatywnie: przejdź w sen po określonym czasie bezczynności
    // if (millis() - lastActivity > SLEEP_AFTER_MS) {
    //     goToDeepSleep();
    // }

    vTaskDelay(1000);
}


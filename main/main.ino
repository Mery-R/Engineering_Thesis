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
#include "TimeConfig.h"
#include "GpsModule.h"


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

        uint64_t now = gpsGetUnixMillis();
        if(now == 0) now = isTimeSynced()?getTimestamp():millis();

        xSemaphoreTake(dataSem, portMAX_DELAY);
        if(gpsOk && gpsHasFix()){
            double lat, lon, elev, speed;
            getGpsData(lat, lon, elev, speed);
            data.lat = lat; data.lon = lon; data.elevation = elev; data.speed = speed;
            data.timestamp = now; data.timestamp_time_source = TIME_GPS;
            data.last_gps_fix_timestamp = now;
            data.error_code &= ~ERR_GPS_NO_FIX;
        } else {
            data.error_code |= ERR_GPS_NO_FIX;
            data.timestamp = now;
            data.timestamp_time_source = (WiFi.status()==WL_CONNECTED)?TIME_WIFI:TIME_LOCAL;
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
        uint64_t now = gpsGetUnixMillis();
        if(now==0) now = isTimeSynced()?getTimestamp():millis();

        xSemaphoreTake(dataSem, portMAX_DELAY);
        if(tempC != DEVICE_DISCONNECTED_C && tempC>-55 && tempC<125){
            data.temp = tempC; data.last_temp_read_timestamp = now;
            data.timestamp = now;
            data.timestamp_time_source = (WiFi.status()==WL_CONNECTED && isTimeSynced())?TIME_WIFI:TIME_LOCAL;
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
            } else {
                Serial.printf("[SD] Saved %d records\n", count);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(Delay_SD));
    }
}

// --- TASK: ThingsBoard ---
void TaskTB(void* pvParameters){
    for(;;){
        if(WiFi.status()==WL_CONNECTED && tbClient.isConnected()){
            int sent = tbClient.sendBatchToTB(sdModule, 20); // wysyła batch z SD, oznacza rekordy jako tb_sent
            if(sent>0) Serial.printf("[TB] Sent %d records from SD\n", sent);
        } else if(WiFi.status()==WL_CONNECTED){
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
            } else {
                Serial.println("[WiFiTask] Reconnection failed, will retry later");
            }
        }
      vTaskDelay(pdMS_TO_TICKS(Delay_TB));
    }
}

// --- SETUP ---
void setup(){
    Serial.begin(115200);
    Serial.println("[INIT] Starting...");

    dataSem = xSemaphoreCreateMutex();

    sdModule.begin();

    wifiManager.begin();

    if(!wifiManager.connectToBest()) {
        Serial.println("[MAIN] Could not connect to any AP, retrying later...");
    }
    else{
      initializeNTP();
    }

    
    
    gpsInit(GPS_BAUDRATE, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("[GPS] Initialized");

    tempSensor.begin();
    Serial.println("[TEMP] DS18B20 initialized");

    xTaskCreate(TaskGPS,  "GPSTask",  16384, NULL, 1,  &gpsModuleTaskHandle);
    xTaskCreate(TaskTemp, "TempTask", 16384, NULL, 1,  &tempModuleTaskHandle);
    xTaskCreate(TaskSD,   "SDTask",   16384, NULL, 1,  &sdModuleTaskHandle);
    xTaskCreate(TaskTB,   "TBTask",   16384, NULL, 1,  &thingsboardTaskHandle);
    xTaskCreate(WiFiTask, "WiFiTask", 16384, NULL, 1,  &wifiTaskHandle);

    startWebServer(80);
    Serial.println("[WEB] Server started");
}

void loop(){}

#include <WiFi.h>
#include "GpsModule.h"
#include "ThingSpeakClient.h"
#include "SdModule.h"
#include "WebServerModule.h"
#include "ThingsBoardClient.h"
#include <esp32_can.h>
#include <can_common.h>

// --- USER SETTINGS ---
const char* WIFI_SSID = "KTO-Rosomak";
const char* WIFI_PASS = "12345678";
WiFiClient client;

// ThingSpeak settings
const char* THINGSPEAK_API_KEY = "OPIFZT6VIE3R7S4Q";
const char* THINGSPEAK_SERVER  = "api.thingspeak.com";
const int   THINGSPEAK_PORT    = 80;
const unsigned long THINGSPEAK_INTERVAL = 15000; // 15 s

// --- ThingsBoard settings ---
const char* THINGSBOARD_SERVER = "demo.thingsboard.io"; // lub IP własnego serwera
const int   THINGSBOARD_PORT   = 80;
const char* THINGSBOARD_TOKEN  = "biaBKRvAy6NypJpbcmcy";
ThingsBoardClient tbClient(THINGSBOARD_SERVER, THINGSBOARD_PORT, THINGSBOARD_TOKEN);

// GPS settings
const int GPS_Baudrate = 115200;
const int GPS_RX = 16;
const int GPS_TX = 17;

// SD module
const uint8_t SD_CS_PIN = 5;
SdModule sdModule(SD_CS_PIN);

// ===== SENSOR DATA STRUCTURE =====
struct SensorData {
  double lat;
  double lon;
  double elevation;
  double speed;
  double temp;
};
SensorData sensorData;

// ===== CAN MESSAGE STRUCTURE =====
struct CANMessage {
  int speed_rpm = 0;
};
CANMessage CANMessageData;

// ===== Timery =====
unsigned long lastGPSUpdate = 0;
const unsigned long GPS_INTERVAL = 1000; // 1 Hz
unsigned long lastSend = 0;

void setup() {
    Serial.begin(115200);

    // --- WiFi ---
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WiFi] Connected to %s\n", WIFI_SSID);
    Serial.print("[WiFi] IP address: ");
    Serial.println(WiFi.localIP());


    // --- GPS ---
    gpsInit(GPS_Baudrate, GPS_RX, GPS_TX);

    // --- ThingSpeak ---
    thingSpeakInit(client, THINGSPEAK_SERVER, THINGSPEAK_PORT);

    // --- SD ---
    if (SD.begin(SD_CS_PIN)) {
        Serial.println("\n[SD] SD card initialized.");
        if (!SD.exists("/data.json")) {
            File f = SD.open("/data.json", FILE_WRITE);
            if (f) {
                f.println("[]");
                f.close();
            }
        }
    } else {
        Serial.println("[SD] SD card initialization failed!");
    }



    // --- Web Server ---
    startWebServer(80);

    // --- CAN (przykład) ---
    CAN0.setCANPins(GPIO_NUM_21, GPIO_NUM_22);
    CAN0.begin(500000);
    CAN0.watchFor();
}

void loop() {
    // --- WiFi reconnect ---
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] disconnected! Reconnecting...");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        unsigned long wifiReconnectStart = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - wifiReconnectStart < 10000) {
            delay(500);
            Serial.print(".");
        }
        Serial.println(WiFi.status() == WL_CONNECTED ? "[WiFi] reconnected!" : "[WiFi] reconnect failed!");
    }

    // --- Obsługa serwera WWW ---
    server.handleClient();

    // --- Odbiór wszystkich bajtów GPS ---
    while (GPSSerial.available() > 0) {
        char c = GPSSerial.read();
        gps.encode(c);
    }

    // --- Aktualizacja GPS co 1 Hz ---
    // --- Zapis + wysyłka co 15 s ---
    if (millis() - lastSend >= THINGSPEAK_INTERVAL) {
    lastSend = millis();

        if (gps.location.isValid()) {
            // Dane GPS
            sensorData.lat = gps.location.lat();
            sensorData.lon = gps.location.lng();
            sensorData.elevation = gps.altitude.meters();
            sensorData.speed = gps.speed.kmph();
            sensorData.temp = 20.0;

            // Zapis JSON
            sdModule.writeJson(sensorData.lat, sensorData.lon, sensorData.elevation,
                            sensorData.speed, sensorData.temp);

            // Wysyłka do ThingsBoard
            tbClient.sendTelemetry(sensorData.lat, sensorData.lon, sensorData.elevation,
                                sensorData.speed, sensorData.temp);
        } else {
            Serial.println("[GPS] No valid fix – skipped.");
        }
    }

    // --- CAN (przykład) ---
    /*
    CAN_FRAME can_message;
    if (CAN0.read(can_message)) {
        if (can_message.id == 378) {
            uint8_t byte1 = can_message.data.byte[0];
            uint8_t byte2 = can_message.data.byte[1];
            int rpm_int = (int)(((byte2 << 8) | byte1) * 3.463);
            CANMessageData.speed_rpm = rpm_int;
            Serial.print("RPM: "); Serial.println(rpm_int);
        }
    }
    */
}

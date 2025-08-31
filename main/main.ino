#include <WiFi.h>
#include "GpsModule.h"
#include "ThingSpeakClient.h"

// --- USER SETTINGS ---
// WiFi credentials
const char* WIFI_SSID = "KTO-Rosomak";
const char* WIFI_PASS = "12345678";

// ThingSpeak settings
const char* THINGSPEAK_API_KEY = "OPIFZT6VIE3R7S4Q";  // Write Key
const char* THINGSPEAK_SERVER  = "api.thingspeak.com";  // Don't Change
const int   THINGSPEAK_PORT    = 80;  // Don't Change
const unsigned long SEND_INTERVAL_MS = 30000; // Interval between data uploads (milliseconds) (min. 15s)

// GPS settings
const int GPS_Boundrate = 115200;
const int GPS_RX = 16; // GPIO16
const int GPS_TX = 17; // GPIO17

// ===== SENSOR DATA STRUCTURE =====
// Extend this struct with more fields if needed
struct SensorData {
    double lat;
    double lon;
    double elevation;
    double speed;
    double temp;
};

WiFiClient client;
SensorData sensorData;
unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConncted to %s WiFi",WIFI_SSID);

  // Initialize modules
  gpsInit(GPS_Boundrate, GPS_RX, GPS_TX);

  thingSpeakInit(client, THINGSPEAK_SERVER, THINGSPEAK_PORT);
}

void printGpsDebug(const SensorData& data) {
  Serial.print("\nGPS: lat=");
  Serial.print(data.lat, 6);
  Serial.print(", lon=");
  Serial.print(data.lon, 6);
  Serial.print(", elevation=");
  Serial.print(data.elevation, 6);
  Serial.print(", speed=");
  Serial.print(data.speed, 6);
  Serial.print(", temp=");
  Serial.println(data.temp, 6);
}

void loop() {
  if (millis() - lastSend > SEND_INTERVAL_MS) {
    if (gpsRead()) {
      sensorData.lat = gps.location.lat();
      sensorData.lon = gps.location.lng();
      sensorData.elevation = gps.altitude.meters();
      sensorData.speed = gps.speed.kmph();
      sensorData.temp = 20;
      printGpsDebug(sensorData);
      sendToThingSpeak(THINGSPEAK_API_KEY, &sensorData, sizeof(sensorData)/sizeof(double), sizeof(double));
      Serial.println("Dane wysłane na ThingSpeak!");
    } else {
      Serial.println("Brak danych GPS - nie wysłano na ThingSpeak!");
    }
    lastSend = millis();
  }

}

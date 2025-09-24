#include <iostream>
#include <cmath>
#include <WiFi.h>
#include "GpsModule.h"
#include "ThingSpeakClient.h"
#include "SdModule.h"
#include <esp32_can.h>
#include <can_common.h>

// --- USER SETTINGS ---
// WiFi credentials
const char* WIFI_SSID = "KTO-Rosomak";
const char* WIFI_PASS = "12345678";
WiFiClient client;

// ThingSpeak settings
const char* THINGSPEAK_API_KEY = "OPIFZT6VIE3R7S4Q";  // Write Key
const char* THINGSPEAK_SERVER  = "api.thingspeak.com";  // Don't Change
const int   THINGSPEAK_PORT    = 80;  // Don't Change
const unsigned long SEND_INTERVAL_MS = 30000; // Interval between data uploads (milliseconds) (min. 15s)

// GPS settings
const int GPS_Boundrate = 115200;
const int GPS_RX = 16; // GPIO16
const int GPS_TX = 17; // GPIO17

// SD module
const uint8_t SD_CS_PIN = 5; // Dostosuj do swojego układu
SdModule sdModule(SD_CS_PIN);

// CAN settings
//#define CAN_RX 21
//#define CAN_TX 22

// ===== SENSOR DATA STRUCTURE =====
// Extend this struct with more fields if needed
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

// Helper variables
unsigned long lastSend = 0;

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected to %s WiFi",WIFI_SSID);

  // GPS init
  gpsInit(GPS_Boundrate, GPS_RX, GPS_TX);

  // ThingSpeak init
  thingSpeakInit(client, THINGSPEAK_SERVER, THINGSPEAK_PORT);

  // SD card init
  if (SD.begin(SD_CS_PIN)) {
    Serial.println("\nSD card initialized.");
    // Zapisz naglowek tylko jesli plik nie istnieje
    if (!SD.exists("/data.csv")) {
      sdModule.writeHeader("lat,lon,elevation,speed,temp");
    }
  } else {
    Serial.println("\nSD card initialization failed!");
  }

  // CAN init
  CAN0.setCANPins(GPIO_NUM_21, GPIO_NUM_22);
  CAN0.begin(500000); // 500Kbps
  CAN0.watchFor();
}

// ===== DEBUG FUNCTIONS =====
// Print GPS data to Serial Monitor for debugging
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


// ===== MAIN LOOP =====
void loop() {
  // Sprawdzanie połączenia WiFi i automatyczne ponowne łączenie
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected! Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long wifiReconnectStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiReconnectStart < 10000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi reconnected!");
    } else {
      Serial.println("WiFi reconnect failed!");
    }
  }

  // Odczyt pojedynczej ramki CAN i przetwarzanie RPM
  CAN_FRAME can_message;

    if (CAN0.read(can_message)) {

      if (can_message.id == 378) {
        uint8_t byte1 = can_message.data.byte[0]; // Second byte (LSB)
        uint8_t byte2 = can_message.data.byte[1]; // Third byte (MSB)

        uint16_t big_endian_value = (byte2 << 8) | byte1;
        int rpm_int = (int)(big_endian_value * 3.463);

        float rpm = (256 * byte1 + byte2) / 4;

    CANMessageData.speed_rpm = rpm_int;

        // Print the result
        Serial.print("RPM: ");
        Serial.println(rpm_int);
        Serial.println(rpm);
      }
    }

  // 2. Odczyt GPS i aktualizacja sensorData
  bool gpsOk = false;
  if (gpsRead()) {
    sensorData.lat = gps.location.lat();
    sensorData.lon = gps.location.lng();
    sensorData.elevation = gps.altitude.meters();
    sensorData.speed = gps.speed.kmph();
    sensorData.temp = 20;
    gpsOk = true;
  } else {
    Serial.println("Blad odczytu GPS!");
  }

  // 3. Zapis danych do pliku CSV na karcie SD (jesli nadszedl czas)
  if ((unsigned long)(millis() - lastSend) > SEND_INTERVAL_MS && gpsOk) {
    printGpsDebug(sensorData);
    String csvLine = String(sensorData.lat, 6) + "," +
                     String(sensorData.lon, 6) + "," +
                     String(sensorData.elevation, 2) + "," +
                     String(sensorData.speed, 2) + "," +
                     String(sensorData.temp, 2);
    if (sdModule.writeData(csvLine)) {
      Serial.println("Dane zapisane na karcie SD.");
    } else {
      Serial.println("Blad zapisu na karcie SD!");
    }

    // 4. Wysylka danych do ThingSpeak (na koncu) - przekazujemy konkretne wartosci
    double fields[5] = {sensorData.lat, sensorData.lon, sensorData.elevation, sensorData.speed, sensorData.temp};
    sendToThingSpeak(THINGSPEAK_API_KEY, fields, 5, sizeof(double));
    Serial.println("Dane wyslane na ThingSpeak!");
    lastSend = millis();
  } else if ((unsigned long)(millis() - lastSend) > SEND_INTERVAL_MS) {
    Serial.println("Brak danych GPS - nie wyslano na ThingSpeak!");
    lastSend = millis();
  }
  delay(1000);
}
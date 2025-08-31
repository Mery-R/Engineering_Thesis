// ThingSpeakClient.cpp
#include "ThingSpeakClient.h"
#include <WiFi.h>

static WiFiClient* ts_client = nullptr;
static const char* ts_server = nullptr;
static int ts_port = 80;

void thingSpeakInit(WiFiClient &client, const char* server, int port) {
  ts_client = &client;
  ts_server = server;
  ts_port = port;
}

// data: pointer to struct, numFields: number of fields, fieldSize: size of each field (in bytes)
void sendToThingSpeak(const char* apiKey, const void* data, size_t numFields, size_t fieldSize) {
  if (!ts_client || !ts_server) return;
  if (!ts_client->connect(ts_server, ts_port)) return;

  // Zakładamy, że data to wskaźnik na struct SensorData { double lat; double lon; }
  const double* vals = (const double*)data;
  String url = "/update?api_key=" + String(apiKey);
  url += "&field1=" + String(vals[0], 6); // lat
  url += "&field2=" + String(vals[1], 6); // lon
  url += "&field3=" + String(vals[2], 6); // elevation
  url += "&field4=" + String(vals[3], 6); // speed
  url += "&field5=" + String(vals[4], 6); // temp

  String req = String("GET ") + url + " HTTP/1.1\r\nHost: " + ts_server + "\r\nConnection: close\r\n\r\n";
  ts_client->print(req);

  // Debug: odczytaj odpowiedź z serwera i wypisz na Serial
  while (ts_client->connected() || ts_client->available()) {
    if (ts_client->available()) {
      String line = ts_client->readStringUntil('\n');
      //Serial.println(line);
    }
  }
  ts_client->stop();
}
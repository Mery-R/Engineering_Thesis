#include <WiFi.h>         // <== DODAJ TO
#include "ThingsBoardClient.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

ThingsBoardClient::ThingsBoardClient(const char* server, int port, const char* token)
    : _server(server), _port(port), _token(token) {}

bool ThingsBoardClient::sendTelemetry(double lat, double lon, double elevation, double speed, double temp) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[ThingsBoard] WiFi not connected!");
        return false;
    }

    HTTPClient http;
    String url = String("http://") + _server + ":" + String(_port) + "/api/v1/" + _token + "/telemetry";

    StaticJsonDocument<256> doc;
    doc["lat"] = lat;
    doc["lon"] = lon;
    doc["elevation"] = elevation;
    doc["speed"] = speed;
    doc["temp"] = temp;

    String payload;
    serializeJson(doc, payload);

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
        Serial.printf("[ThingsBoard] Sent: %s | Response: %d\n", payload.c_str(), httpResponseCode);
    } else {
        Serial.printf("[ThingsBoard] Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
    return httpResponseCode == 200 || httpResponseCode == 204;
}

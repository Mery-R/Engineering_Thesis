#ifndef THINGSBOARD_CLIENT_H
#define THINGSBOARD_CLIENT_H

#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

class ThingsBoardClient {
public:
    ThingsBoardClient(const char* server, int port, const char* token);
    bool sendTelemetry(double lat, double lon, double elevation, double speed, double temp);

private:
    const char* _server;
    int _port;
    const char* _token;
};

#endif

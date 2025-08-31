#pragma once
#include <WiFi.h>

void thingSpeakInit(WiFiClient &client, const char* server, int port);
// Send any struct as fields to ThingSpeak (fields: field1, field2, ...)
// Pass pointer to data, number of fields, and their order is the order in struct
void sendToThingSpeak(const char* apiKey, const void* data, size_t numFields, size_t fieldSize);

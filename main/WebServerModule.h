#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

extern WebServer server;

void startWebServer(uint16_t port = 80);

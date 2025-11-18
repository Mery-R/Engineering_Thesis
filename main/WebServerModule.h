#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

// Forward declaration of the global WebServer instance
extern WebServer server;

// Function to initialize and start the web server
void startWebServer(uint16_t port);

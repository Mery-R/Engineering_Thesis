#pragma once
#include <Arduino.h>
#include "driver/twai.h"

// CAN Module (TWAI for ESP32)
class CanModule {
public:
    CanModule(int rxPin, int txPin);

    bool begin(); // Initialize and start CAN
    bool getMessage(twai_message_t &message); // Receive message (non-blocking)
    float readSignal(const twai_message_t &message, uint32_t id, int startBit, int length, bool isBigEndian, float factor); // Generic signal extraction
    void stop(); // Stop and uninstall driver

private:
    int _rxPin;
    int _txPin;
    bool _isInitialized;
};

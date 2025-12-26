#pragma once
#include <Arduino.h>
#include "driver/twai.h"

class CanModule {
public:
    CanModule(int rxPin, int txPin);
    bool begin();
    bool getMessage(twai_message_t &message);
    float scaleSpeed(const twai_message_t &message);
    void stop();

private:
    int _rxPin;
    int _txPin;
    bool _isInitialized;
};

#include "CanModule.h"

CanModule::CanModule(int rxPin, int txPin) : _rxPin(rxPin), _txPin(txPin), _isInitialized(false) {}

// -----------------------------------------------------
// --------------- Public Methods ----------------------
// -----------------------------------------------------

bool CanModule::begin() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)_txPin, (gpio_num_t)_rxPin, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // Install TWAI driver
    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        Serial.println("[CAN] Failed to install driver");
        return false;
    }

    // Start TWAI driver
    if (twai_start() != ESP_OK) {
        Serial.println("[CAN] Failed to start driver");
        return false;
    }

    _isInitialized = true;
    Serial.println("[CAN] Initialized and started at 500kbps");
    return true;
}

bool CanModule::getMessage(twai_message_t &message) {
    if (!_isInitialized) return false;
    
    // Receive message with zero timeout (non-blocking)
    esp_err_t res = twai_receive(&message, 0);
    return (res == ESP_OK);
}

float CanModule::scaleSpeed(const twai_message_t &message) {
    // Placeholder logic for scaling speed from CAN frame.
    // Assuming ID 0x123 contains speed in first 2 bytes (uint16_t), big-endian.
    // Scale factor example: 0.1 (e.g. 1000 = 100.0 km/h)
    if (message.identifier == 0x123 && message.data_length_code >= 2) {
        uint16_t rawSpeed = (message.data[0] << 8) | message.data[1];
        return (float)rawSpeed * 0.1f;
    }
    return -1.0f; // Indicate no speed found or invalid ID
}

void CanModule::stop() {
    if (_isInitialized) {
        twai_stop();
        twai_driver_uninstall();
        _isInitialized = false;
        Serial.println("[CAN] Stopped");
    }
}

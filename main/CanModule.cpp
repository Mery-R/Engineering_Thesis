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

float CanModule::readSignal(const twai_message_t &message, uint32_t id, int startBit, int length, bool isBigEndian, float factor) {
    if (message.identifier != id) {
        return -1.0f; // Wrong ID
    }

    uint64_t rawValue = 0;

    if (isBigEndian) {
        // Big Endian (Motorola-ish / Network Order)
        // Treated as a stream of bits MSB first:
        // Index 0 = Byte0.Bit7, Index 7 = Byte0.Bit0, Index 8 = Byte1.Bit7...
        // We read 'length' bits starting from 'startBit' and shift them into rawValue.
        for (int i = 0; i < length; i++) {
            int pos = startBit + i;
            int byteIdx = pos / 8;
            int bitIdx = 7 - (pos % 8); // 0->7, 7->0

            if (byteIdx < message.data_length_code) {
                int bit = (message.data[byteIdx] >> bitIdx) & 1;
                rawValue = (rawValue << 1) | bit;
            }
        }
    } else {
        // Little Endian (Intel)
        // Treated as a stream of bits LSB first:
        // Index 0 = Byte0.Bit0, Index 7 = Byte0.Bit7, Index 8 = Byte1.Bit0...
        // We read 'length' bits starting from 'startBit' and fill rawValue from LSB up.
        for (int i = 0; i < length; i++) {
            int pos = startBit + i;
            int byteIdx = pos / 8;
            int bitIdx = pos % 8; // 0->0, 7->7

            if (byteIdx < message.data_length_code) {
                int bit = (message.data[byteIdx] >> bitIdx) & 1;
                rawValue |= ((uint64_t)bit << i);
            }
        }
    }

    return (float)rawValue * factor;
}

void CanModule::stop() {
    if (_isInitialized) {
        twai_stop();
        twai_driver_uninstall();
        _isInitialized = false;
        Serial.println("[CAN] Stopped");
    }
}

#include "WiFiManager.h"

WiFiManager::WiFiManager(const std::vector<WiFiConfig>& configs)
    : _configs(configs)
{
}

void WiFiManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);

    Serial.println("[WiFi] Initialized");

    // Rejestracja eventów ESP32
    esp_event_loop_create_default();

    esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &WiFiEventHandler,
        this,
        NULL
    );

    esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &WiFiEventHandler,
        this,
        NULL
    );
}


void WiFiManager::startScan() {
    Serial.println("[WiFi] Scanning...");
    int n = WiFi.scanNetworks(false, true);

    _scanned.clear();
    for (int i = 0; i < n; ++i) {
        ScannedNetwork sn;
        sn.ssid = WiFi.SSID(i);
        sn.rssi = WiFi.RSSI(i);
        _scanned.push_back(sn);
        if (debug) Serial.printf("  %s (%d dBm)\n", sn.ssid.c_str(), sn.rssi);
    }

    if (n == 0) Serial.println("[WiFi] No networks found");
}

const WiFiConfig* WiFiManager::chooseBestAP() {
    const WiFiConfig* best = nullptr;
    int bestRSSI = -1000;

    for (auto& cfg : _configs) {
        for (auto& sn : _scanned) {
            if (sn.ssid == cfg.ssid && sn.rssi > bestRSSI) {
                bestRSSI = sn.rssi;
                best = &cfg;
            }
        }
    }
    return best;
}

bool WiFiManager::connectToBest() {
    startScan();

    const WiFiConfig* cfg = chooseBestAP();
    if (!cfg) {
        return false;
    }

    connectTo(cfg);
    return true;
}

void WiFiManager::connectTo(const WiFiConfig* cfg) {
    Serial.printf("[WiFi] Connecting to %s...\n", cfg->ssid);
    WiFi.begin(cfg->ssid, cfg->password);
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    Serial.println("[WiFi] Disconnected");
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

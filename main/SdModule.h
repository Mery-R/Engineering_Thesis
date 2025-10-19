#ifndef SD_MODULE_H
#define SD_MODULE_H

#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>

class SdModule {
public:
    SdModule(uint8_t csPin);
    bool begin();
    bool writeJson(double lat, double lon, double elev, double speed, double temp);

private:
    uint8_t _csPin;
    String _filename;
};

#endif

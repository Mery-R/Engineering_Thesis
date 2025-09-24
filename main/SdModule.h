#ifndef SD_MODULE_H
#define SD_MODULE_H

#include <SD.h>
#include <SPI.h>


class SdModule {
public:
    SdModule(uint8_t csPin);
    bool writeData(const String& csvLine);
    bool writeHeader(const String& headerLine);
private:
    uint8_t _csPin;
    String _filename;
};

#endif // SD_MODULE_H

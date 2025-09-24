#include "SdModule.h"


SdModule::SdModule(uint8_t csPin) : _csPin(csPin), _filename("/data.csv") {}

bool SdModule::writeHeader(const String& headerLine) {
    if (!SD.exists(_filename)) {
        File file = SD.open(_filename, FILE_WRITE);
        if (file) {
            file.println(headerLine);
            file.close();
            return true;
        } else {
            Serial.println("[SD] Nie udało się utworzyć pliku nagłówka!");
        }
    }
    return false;
}

bool SdModule::writeData(const String& csvLine) {
    Serial.print("[SD] Próbuję otworzyć plik: ");
    Serial.println(_filename);
    File file = SD.open(_filename, FILE_WRITE);
    if (file) {
        Serial.println("[SD] Plik otwarty poprawnie, zapisuję dane...");
        file.println(csvLine);
        file.close();
        return true;
    } else {
        Serial.println("[SD] Nie udało się otworzyć pliku do zapisu!");
        if (SD.exists(_filename)) {
            Serial.println("[SD] Plik istnieje, ale nie można go otworzyć do zapisu.");
        } else {
            Serial.println("[SD] Plik nie istnieje i nie można go utworzyć.");
        }
    }
    return false;
}

# GPS Tracker IoT – Praca Inżynierska

## Opis projektu
Projekt inżynierski polega na zaprojektowaniu i implementacji urządzenia typu **GPS Tracker** opartego na mikrokontrolerze **ESP32**, które umożliwia:
- zbieranie danych lokalizacyjnych z modułu GPS,
- wysyłanie ich do chmury (ThingSpeak) w czasie rzeczywistym,
- monitorowanie lokalizacji poprzez panel online,
- zasilanie z akumulatora litowo-jonowego z układem ładowania.

Celem projektu jest stworzenie niedrogiej, otwartej i łatwo rozszerzalnej platformy IoT do monitorowania położenia obiektów (np. pojazdów, rowerów, sprzętu).

---

## Wykorzystane technologie

### Sprzęt
- ESP32-DevKitC-32D – główny mikrokontroler z Wi-Fi i Bluetooth
- Moduł GPS (NEO-6M lub kompatybilny) – odbiornik sygnału GNSS
- Akumulator Li-Ion NCR18650B 3400 mAh – zasilanie mobilne
- TP4056 USB-C – układ ładowania akumulatora
- BMS PCM 3,7V 8A – zabezpieczenie akumulatora
- Moduł karty microSD – zapis danych offline
- Czujnik temperatury DS18B20 (opcjonalny) – monitoring środowiskowy

### Oprogramowanie
- Arduino IDE lub PlatformIO – środowisko programistyczne
- C++ (Arduino Core for ESP32) – główny język implementacji
- ThingSpeak API – chmura do wizualizacji danych
- TinyGPS++ – parser ramek NMEA z modułu GPS
- Biblioteki ESP32 – obsługa Wi-Fi, HTTP, I2C, UART

---

## Architektura systemu

1. Odbiór danych GPS – moduł NEO-6M przesyła dane w formacie NMEA do ESP32.
2. Przetwarzanie danych – ESP32 parsuje dane i wyodrębnia:
   - współrzędne (lat, lon),
   - prędkość,
   - wysokość,
   - dokładność,
   - czas UTC.
3. Wysyłanie danych do chmury – ESP32 łączy się z Wi-Fi i publikuje dane na ThingSpeak poprzez API.
4. Zapis offline – równolegle dane mogą być logowane na kartę SD.
5. Prezentacja danych – użytkownik śledzi lokalizację przez dashboard ThingSpeak.

---

## Funkcjonalności
- Odczyt pozycji GPS w czasie rzeczywistym
- Automatyczne wysyłanie danych do chmury (ThingSpeak)
- Backup danych na kartę SD
- Zasilanie akumulatorowe z możliwością ładowania przez USB-C
- Ochrona akumulatora dzięki układowi BMS
- Modułowa budowa – możliwość podłączenia dodatkowych czujników IoT
- Obsługa komunikacji Wi-Fi oraz potencjalna rozbudowa o LoRa lub GSM

---

## Schemat połączeń (hardware)

- ESP32 ↔ GPS (NEO-6M) – UART
- ESP32 ↔ microSD – SPI
- ESP32 ↔ DS18B20 – OneWire
- ESP32 ↔ TP4056 / BMS – zasilanie bateryjne
- ESP32 ↔ Wi-Fi – komunikacja z chmurą

---

## Instrukcja uruchomienia

1. Sklonuj repozytorium:
   ```bash
   git clone https://github.com/<twoj-nick>/gps-tracker.git
   cd gps-tracker
2. Otwórz projekt w Arduino IDE lub PlatformIO.

3. Zainstaluj wymagane biblioteki:

    * TinyGPS++

    * WiFi.h

    * HTTPClient.h

    * SD.h

4. Skonfiguruj plik config.h:
    ```bash
    #define WIFI_SSID "TwojaSiec"
    #define WIFI_PASS "TwojeHaslo"
    #define API_KEY   "THING_SPEAK_WRITE_API_KEY"  
5. Wgraj kod na ESP32.

6. Uruchom urządzenie i obserwuj dane na panelu ThingSpeak.

## Wizualizacja danych

Na panelu ThingSpeak można monitorować w czasie rzeczywistym:

* aktualną lokalizację na mapie,

* historię trasy,

* prędkość i wysokość,

* dodatkowe czujniki (np. temperaturę).

## Możliwości rozwoju

* Implementacja komunikacji LoRa dla obszarów bez Wi-Fi

* Wysyłanie danych SMS-em poprzez moduł GSM (SIM800L)

* Integracja z Google Maps API w aplikacji mobilnej

* Tryb uśpienia ESP32 dla wydłużenia czasu pracy baterii

* Dodanie czujników IMU (akcelerometr, żyroskop) dla analizy ruchu

* Wersja miniaturowa z obudową wydrukowaną w 3D

## Dokumentacja i źródła

* ESP32 Arduino Core

* ThingSpeak API

*  TinyGPS++

## Autor

#### Miłosz Stec – 2025

Projekt zrealizowany w ramach pracy inżynierskiej
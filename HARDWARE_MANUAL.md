# 🛠️ Instrukcja Montażu i Dokumentacja Sprzętowa

Dokument zawiera listę części (BOM), schemat połączeń oraz instrukcję montażu dla systemu akwizycji danych GPS/CAN opartego na ESP32.

## 1. Zestawienie Materiałów (BOM)

Ceny są szacunkowe na podstawie wybranych podzespołów (stan na 11.2025).

|   Lp.    | Nazwa Elementu                      | Funkcja                                                                                                      | Szac. Koszt (PLN) | Link                                                                                                                                                             |
| :------: | :---------------------------------- | :----------------------------------------------------------------------------------------------------------- | :---------------: | :--------------------------------------------------------------------------------------------------------------------------------------------------------------- |
|    1     | **ESP32-DevKitC-32D v4**            | Główny mikrokontroler (WiFi/BT)                                                                              |     25,00 zł      | [Kamami](https://kamami.pl/esp32/573333-esp32-devkitc-32d-v4-plytka-rozwojowa-iot-z-modulem-wi-fi-esp-wroom-32d-5906623456529.html)                              |
|    2     | **Moduł GNSS LC76G**                | Odbiornik GPS/GLONASS/Galileo (UART)                                                                         |     35,00 zł      | [Kamami](https://kamami.pl/moduly/1185254-lc76G-multi-gnss-module-supports-gps-bds-glonass-galileo-qzss-5906623423767.html)                                      |
|    3     | **Moduł Czytnika microSD**          | Zapis danych offline (SPI)                                                                                   |     10,00 zł      | [Kamami](https://kamami.pl/czytniki-kart-pamieci/586607-modul-czytnika-kart-pamieci-microsd-z-konwerterem-poziomow-logicznych-5906623468010.html)                |
|    4     | **Karta microSD 32GB**              | Nośnik danych (Goodram Class 10)                                                                             |     20,00 zł      | [Kamami](https://kamami.pl/karty-pamieci/563954-karta-pamieci-goodram-micro-sdhc-32gb-class-10-z-adapterem-5908267930144.html)                                   |
|    5     | **DS18B20 (Wodoodporny)**           | Czujnik temperatury (1-Wire)                                                                                 |     15,00 zł      | [Kamami](https://kamami.pl/czujniki-temperatury/211331-modds18B20-czujnik-temperatury-ds18B20-w-wodoodpornej-obudowie-5906623441044.html)                        |
|    6     | **Moduł CAN-PAL (TJA1051)**         | Komunikacja z magistralą pojazdu                                                                             |     12,00 zł      | [Kamami](https://kamami.pl/konwertery-can/1184871-can-pal-modul-z-transceiverem-can-tja1051t3-5906623431632.html)                                                |
|    7     | **Przetwornica Step-Down (5V 3A)**  | Zasilanie z instalacji auta (12V -> 5V)                                                                      |     10,00 zł      | [Kamami](https://kamami.pl/step-down/585139-modul-przetwornicy-dc-dc-step-down-5v-3a-5906623467921.html)                                                         |
|    8     | **Moduł Ładowania + Boost (USB-C)** | Zasilanie awaryjne z ogniwa Li-Ion                                                                           |     15,00 zł      | [Allegro](https://allegro.pl/oferta/gotowy-modul-zasilania-akumulatorowego-usb-c-5v-1a-ogniwo-li-ion-1s-18650-17249844742)                                       |
|    9     | **Akumulator NCR18650B**            | Ogniwo Li-Ion 3400mAh                                                                                        |     25,00 zł      | [Kamami](https://kamami.pl/akumulatory/583639-akumulator-ncr18650B-panasonic-3400mah-li-ion-37v-68a-5902186324770.html)                                          |
|    10    | **Płytka uniwersalna (dwustronna)** | Baza montażowa (2035 otworów)                                                                                |     12,00 zł      | [Kamami](https://kamami.pl/plytki-uniwersalne/583014-dwustronna-plytka-uniwersalna-2035-otworow-5906623460915.html)                                              |
|    11    | **Obudowa ZOBD (Zaślepka)**         | Element montażowy typu OBD                                                                                   |      5,00 zł      | [Kamami](https://kamami.pl/akcesoria-do-obudow/1191904-zobd-cap-plug-abs-zlacze-obd-w-postaci-zaslepki-do-obudow-zobd-i-zobd-mini-wykonana-z-czarnego-absu.html) |
|    12    | **Elementy pasywne i drobne**       | 3x LED, 3x rezystor 220Ω, 1x rezystor 4.7kΩ, 2x dioda Schottky (np. 1N5817), przełącznik, goldpiny, przewody |     15,00 zł      | -                                                                                                                                                                |
| **SUMA** | **Całkowity koszt części:**         | -                                                                                                            |  **~199,00 zł**   | -                                                                                                                                                                |

---

## 2. Schemat Połączeń (Pinout)

### A. Układ Zasilania (UPS - Suma logiczna)

1. **Wejście 12V (Auto):** Podłączone do wejścia przetwornicy Step-Down.
2. **Wejście USB-C:** Podłączenie po przez ładowarkę sieciową.
3. **Wybór Zasilania:** Akumulator powinien być ładowany tylko przez jedno wejście.(To znaczy USB-C lub OBD2)
4. **Masa (GND):** Wszystkie masy połączone razem.

### B. Peryferia (GPIO ESP32)

| Moduł             | Pin Modułu     | Pin ESP32     | Uwagi                           |
| :---------------- | :------------- | :------------ | :------------------------------ |
| **GPS (LC76G)**   | TX             | GPIO 16 (RX2) |                                 |
|                   | RX             | GPIO 17 (TX2) |                                 |
|                   | PPS            | GPIO 32       | Opcjonalnie                     |
| **MicroSD**       | CS             | GPIO 5        |                                 |
|                   | MOSI           | GPIO 23       |                                 |
|                   | MISO           | GPIO 19       |                                 |
|                   | SCK            | GPIO 18       |                                 |
| **CAN (TJA1051)** | TX (CTX)       | GPIO 21       |                                 |
|                   | RX (CRX)       | GPIO 22       |                                 |
| **DS18B20**       | DATA           | GPIO 4        | Wymagany rezystor 4.7kΩ do 3.3V |
| **Diody LED**     | WiFi (Anoda)   | GPIO 25       | Przez rezystor 220Ω do GND      |
|                   | GPS (Anoda)    | GPIO 26       | Przez rezystor 220Ω do GND      |
|                   | SD/Err (Anoda) | GPIO 27       | Przez rezystor 220Ω do GND      |
| **Przycisk**      | Pin 1          | GPIO 33       | Drugi pin do GND                |

---

## 3. Instrukcja Montażu

1. **Przygotowanie płytki:** Rozmieść elementy na płytce uniwersalnej. Zostaw miejsce na akumulator i przetwornicę.
2. **Sekcja zasilania:**
   * Wlutuj przetwornicę Step-Down i ustaw jej napięcie wyjściowe na 5.1V (przed podłączeniem ESP32!).
   * Wlutuj diode Schottky'ego.
3. **Lutowanie ESP32:** Zalecane użycie listew goldpin (żeńskich), aby moduł był wymienny.
4. **Podłączenie modułów:**
   * Podłącz moduły GPS, SD i CAN zgodnie z tabelą pinów.
   * Pamiętaj o rezystorze pull-up (4.7kΩ) dla czujnika temperatury DS18B20.
5. **Diody LED:** Podłącz diody przez rezystory ograniczające prąd.
6. **Testy:** Sprawdź zasilanie multimetrem przed włożeniem ESP32 w podstawkę.
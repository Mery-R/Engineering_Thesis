# Engineering Thesis - GPS Data Collection System

## Project Overview

This is an ESP32-based data acquisition system that collects GPS coordinates, temperature data, and transmits them to ThingsBoard IoT platform via MQTT over Wi-Fi. The system also provides local data logging to SD card for offline operation and a web server for real-time map visualization.

## System Architecture

### Main Components

1. **main.ino** - Main entry point with FreeRTOS task management
2. **GpsModule.cpp/h** - GPS receiver communication (UART, TinyGPSPlus)
3. **ThingsBoardClient.cpp/h** - MQTT telemetry transmission
4. **SdModule.cpp/h** - SD card data logging
5. **WebServerModule.cpp/h** - Web server with Leaflet map visualization
6. **DS18B20 Temperature Sensor** - 1-Wire protocol sensor reading

### FreeRTOS Tasks

The system runs 4 concurrent tasks:

1. **TaskGPS** (Priority 1)
   - Reads GPS data every 10 seconds (Delay_GPS = 10000ms)
   - Maintains GPS fix status
   - Updates global sensor data structure with semaphore protection
   - Falls back to last known position if no fix available

2. **TaskTemp** (Priority 1)
   - Reads DS18B20 temperature sensor every 5 seconds (Delay_Temp = 5000ms)
   - Validates temperature readings (-55°C to 125°C)
   - Handles sensor disconnection gracefully

3. **TaskSD** (Priority 1)
   - Logs sensor data to SD card every 10 seconds (Delay_SD = 10000ms)
   - Only writes to SD when Wi-Fi/ThingsBoard are unavailable
   - Reduces SD wear during normal operation

4. **TaskServer** (Priority 1)
   - Manages web server on port 80
   - Handles ThingsBoard MQTT transmission every 15 seconds (Delay_TB = 15000ms)
   - Processes SD card backlog first (up to 20 records per batch)
   - Then sends RAM ring buffer contents
   - Re-appends failed records to SD for later retry

### Data Flow

```
GPS + Temp Sensors
    ↓
TaskGPS & TaskTemp → Global SensorData (protected by semaphore)
    ↓
Ring Buffer (last 10 readings in RAM)
    ↓
TaskServer
    ├→ SD Card (for offline storage)
    └→ ThingsBoard (via MQTT)
```

### Data Storage

#### SD Card Format
- **File**: `/data_log.txt`
- **Format**: JSON Lines (one JSON object per line)
- **Structure**:
  ```json
  {
    "lat": 52.123456,
    "lon": 21.654321,
    "elevation": 125.50,
    "speed": 45.30,
    "temp": 22.50,
    "current_time": 1637000000,
    "time_source": 1,
    "last_gps_fix_timestamp": 1637000000,
    "last_temp_read_timestamp": 1637000000,
    "error_code": 0
  }
  ```

#### ThingsBoard MQTT Topic
- **Topic**: `v1/devices/me/telemetry`
- **Payload**: JSON with lat, lon, elevation, speed, temp, ts (timestamp)

### Error Handling

The system uses a bitfield-based error code:

```cpp
#define ERR_GPS_NO_FIX   (1 << 0)  // Bit 0: GPS fix unavailable
#define ERR_TEMP_FAIL    (1 << 1)  // Bit 1: Temperature sensor failure
#define ERR_SD_FAIL      (1 << 2)  // Bit 2: SD card write failure
#define ERR_TB_FAIL      (1 << 3)  // Bit 3: ThingsBoard transmission failure
```

### Time Sources

The system supports three time sources (in priority order):

1. **TIME_GPS** - GPS-derived time (most accurate, requires GPS fix)
2. **TIME_WIFI** - NTP via Wi-Fi (when connected and synced)
3. **TIME_LOCAL** - System millis() fallback (least accurate)

## Hardware Configuration

### GPIO Pinout (ESP32)

| Function | GPIO | Notes |
|----------|------|-------|
| GPS RX | GPIO16 | Serial1 input |
| GPS TX | GPIO17 | Serial1 output |
| SD CS | GPIO5 | Chip select for SD SPI |
| SPI MOSI | GPIO23 | Serial Peripheral Interface |
| SPI MISO | GPIO19 | Serial Peripheral Interface |
| SPI SCK | GPIO18 | Serial Peripheral Interface |
| DS18B20 | GPIO4 | OneWire temperature sensor |

### WiFi Configuration

- **SSID**: KTO-Rosomak
- **Password**: 12345678 (change in production!)

### ThingsBoard Configuration

- **Server**: demo.thingsboard.io
- **Port**: 1883 (MQTT)
- **Client ID**: esp32_test
- **Username**: user123
- **Password**: haslo123 (change in production!)

## Web Interface

The web server provides a live map at `http://<ESP32_IP>/`

Features:
- Real-time GPS trace visualization with Leaflet.js
- Configurable number of points (1-100)
- Color coding:
  - Red: Latest position
  - Green: Recent positions (last 10)
  - Blue: Older positions
- Reset view button to auto-fit map bounds

API Endpoint: `/gpsdata`
- Returns JSON Lines format compatible with the storage format

## Dependencies

### Arduino Libraries (required in `libraries.h` or IDE)

- **WiFi** - ESP32 built-in
- **WebServer** - ESP32 built-in
- **SD** - ESP32 built-in
- **SPI** - ESP32 built-in
- **OneWire** - For DS18B20 temperature sensor
- **DallasTemperature** - DS18B20 driver
- **TinyGPSPlus** - GPS NMEA parser
- **PubSubClient** - MQTT client
- **ArduinoJson** - JSON serialization

## Ring Buffer Design

A 10-entry ring buffer in RAM provides:
- Fast access to recent data
- Efficient memory usage
- FIFO ordering for transmission to ThingsBoard

Oldest entries are popped first and transmitted to ThingsBoard, then removed from the buffer.

## Synchronization

All access to the global `SensorData` structure is protected by a mutex semaphore (`dataSem`) to prevent race conditions between sensor reading tasks and the transmission task.

Ring buffer operations are also protected by their own semaphore (`ringSem`).

## Compilation Notes

- **Platform**: ESP32 (Arduino IDE or PlatformIO)
- **Board**: ESP32 Dev Module or compatible
- **Flash Size**: 4MB minimum recommended
- **Baud Rate**: 115200 for Serial (debugging)

## Future Improvements

1. Configurable SD write threshold (currently offline-only)
2. Web interface for configuration changes
3. Battery monitoring and low-power mode
4. SD card cleanup based on file size limits
5. Multiple GPS fix averaging
6. Data encryption for MQTT transmission
7. OTA (Over-The-Air) firmware updates

## Troubleshooting

### GPS No Fix
- Check antenna connection
- Ensure GPS module is powered correctly
- Wait for initial acquisition (5 minutes timeout)
- Check serial communication on pins 16/17

### SD Card Issues
- Verify CS pin (GPIO5) connection
- Check SPI pin configuration (18/19/23)
- Format card as FAT32
- Ensure sufficient free space

### ThingsBoard Connection Fails
- Verify Wi-Fi credentials
- Check MQTT broker availability
- Confirm credentials on broker
- Check network firewall rules

### High Temperature Readings
- Verify DS18B20 connection to GPIO4
- Check OneWire pull-up resistor (4.7kΩ recommended)
- Ensure sensor hasn't been disconnected

## License

This project is part of an engineering thesis. Modify and use as needed for educational purposes.

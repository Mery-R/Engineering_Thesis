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

The system uses a **Coordinator-driven** architecture where a central task triggers sensors and manages data flow.

1. **CoordinatorTask** (Priority 2)
   - Runs every 15 seconds (`Delay_MAIN = 15000ms`)
   - Prepares timestamp
   - Triggers **TaskGPS** and **TaskTemp** via notifications
   - Waits for both sensors to complete readings
   - Pushes a snapshot of `SensorData` to the **Ring Buffer**
   - Triggers **TaskSD** if buffer reaches batch size

2. **TaskGPS** (Priority 1)
   - Waits for notification from Coordinator
   - Wakes GPS module and processes NMEA data for up to 5 seconds
   - Updates global `SensorData` (lat, lon, speed, elevation)
   - Signals completion back to Coordinator

3. **TaskTemp** (Priority 1)
   - Waits for notification from Coordinator
   - Requests temperature from DS18B20
   - Updates global `SensorData` (temp)
   - Signals completion back to Coordinator

4. **TaskSD** (Priority 1)
   - Waits for notification (batch ready or forced flush)
   - Pops a batch of records (Batch Size = 2) from Ring Buffer
   - Appends data to SD card in JSON Lines format
   - Notifies **TaskTB** that new data is available

5. **TaskTB** (ThingsBoard) (Priority 1)
   - Waits for notification or timeout (1s)
   - Connects to MQTT if needed
   - Sends data from Ring Buffer (if full/forced)
   - Sends unsent data from SD card (every `sdSendInterval`)

6. **TaskWiFi** (Priority 1)
   - Monitors WiFi connection
   - Handles reconnections if link is lost

7. **ButtonTask** (Priority 1)
   - Debounces wake button press
   - Signals **GoToSleepTask**

8. **GoToSleepTask** (Priority 2)
   - Orchestrates deep sleep sequence
   - Forces flush of SD and ThingsBoard buffers
   - Enters deep sleep (wakes on button press)

### Data Flow

```
Coordinator (Every 15s)
    ├→ Trigger GPS -> Read -> Update Global Data
    ├→ Trigger Temp -> Read -> Update Global Data
    └→ Collect Snapshot -> Push to Ring Buffer (Size 4)
                            ↓
                        TaskSD (Batch Size 2)
                            ├→ Write to SD (/data.jsonl)
                            └→ Notify TaskTB
                                    ↓
                                TaskTB
                                    ├→ Send Ring Buffer (Real-time)
                                    └→ Send SD Backlog (Offline recovery)
```

### Data Storage

#### SD Card Format
- **File**: `/data.jsonl`
- **Format**: JSON Lines (one JSON object per line)
- **Structure**:
  ```json
  {
    "lat": 52.123456,
    "lon": 21.654321,
    "elevation": 125.50,
    "speed": 45.30,
    "temp": 22.50,
    "timestamp": 1637000000000,
    "time_source": 1,
    "last_gps_fix_timestamp": 1637000000000,
    "last_temp_read_timestamp": 1637000000000,
    "error_code": 0,
    "tb_sent": false
  }
  ```

#### ThingsBoard MQTT Topic
- **Topic**: `v1/devices/me/telemetry`
- **Payload**:
  ```json
  {
    "ts": 1637000000000,
    "values": {
        "lat": 52.123456,
        "lon": 21.654321,
        "temp": 22.50,
        ...
    }
  }
  ```

### Error Handling

Bitfield-based error codes in `SensorData.error_code`:

```cpp
#define ERR_GPS_NO_FIX   (1 << 0)
#define ERR_TEMP_FAIL    (1 << 1)
#define ERR_SD_FAIL      (1 << 2)
#define ERR_TB_FAIL      (1 << 3)
```

## Hardware Configuration

### GPIO Pinout (ESP32)

| Function | GPIO | Notes |
|----------|------|-------|
| **WiFi LED** | 25 | Status Indicator |
| **GPS LED** | 26 | Status Indicator |
| **SD LED** | 27 | Status Indicator |
| **GPS RX** | 16 | Connect to GPS TX |
| **GPS TX** | 17 | Connect to GPS RX |
| **GPS PPS** | 32 | Pulse Per Second |
| **SD CS** | 5 | SPI Chip Select |
| **DS18B20** | 4 | OneWire Data |
| **Wake Button** | 33 | Input Pullup |

### Configuration Constants (`main.ino`)

- **Delay_MAIN**: 15000 ms (Sampling Interval)
- **Delay_SD**: 15000 ms
- **Delay_WIFI**: 30000 ms
- **RINGBUFFER_CAPACITY**: 4
- **BATCH_SIZE**: 2

## Web Interface

The web server runs on port 80 and serves a map visualization.
- **URL**: `http://<ESP32_IP>/`
- **Endpoint**: `/gpsdata` (Reads from `/data.json`)
- **Note**: The web server is started in `setup()` but `server.handleClient()` is currently **NOT** called in the main loop or any task, so the web interface may be unresponsive in the current code version.

## Dependencies

- **WiFi**
- **WebServer**
- **SD**
- **SPI**
- **OneWire**
- **DallasTemperature**
- **TinyGPSPlus**
- **PubSubClient**
- **ArduinoJson**

## License

This project is part of an engineering thesis.

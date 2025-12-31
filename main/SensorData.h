#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Error codes
#define ERR_GPS_NO_FIX   (1 << 0)
#define ERR_TEMP_FAIL    (1 << 1)
#define ERR_SD_FAIL      (1 << 2)
#define ERR_TB_FAIL      (1 << 3)

// X-Macro definition for Sensor Data
// XX(Type, Name, JsonKey, IsTelemetry, IsSd)
#define SENSOR_DATA_MAP(XX) \
    XX(uint64_t, ts,                       "ts",                       false, true) \
    XX(int,      ts_source,                "ts_source",                true,  true) \
    XX(double,   lat,                      "lat",                      true,  true) \
    XX(double,   lon,                      "lon",                      true,  true) \
    XX(double,   alt,                      "alt",                      true,  true) \
    XX(double,   vel,                      "vel",                      true,  true) \
    XX(float,    temp,                     "temp",                     true,  true) \
    XX(float,    can_vel,                  "can_vel",                  true,  true) \
    XX(uint64_t, lgr_ts,                   "lgr_ts",                   false, true) \
    XX(uint64_t, ltr_ts,                   "ltr_ts",                   false, true) \
    XX(uint64_t, lcr_ts,                   "lcr_ts",                   false, true) \
    XX(uint8_t,  ec,                       "ec",                       true,  true) \
    XX(bool,     tb_sent,                  "tb_sent",                  false, true) \
    XX(int,      rssi,                     "rssi",                     true,  true)

    //X-Macro fields
    //Timestamp
    //Timestamp source
    //Latitude
    //Longitude
    //Altitude
    //Velocity
    //Temperature
    //CAN velocity
    //Last GPS fix timestamp
    //Last temperature read timestamp
    //Last CAN message received timestamp
    //Error codes
    //ThingsBoard data flag
    //RSSI (Signal Strength)

// SensorData structure definition
struct SensorData {
#define XX_FIELD(Type, Name, Key, IsTel, IsSd) Type Name;
    SENSOR_DATA_MAP(XX_FIELD)
#undef XX_FIELD
};

// 1. Function to convert SensorData to ThingsBoard JSON format
// {"ts": <timestamp>, "values": { <telemetry keys only> }}
inline void sensorDataToTb(const SensorData &data, JsonObject &root) {
    root["ts"] = data.ts;
    JsonObject values = root.createNestedObject("values");

#define XX_TO_TB(Type, Name, Key, IsTel, IsSd) \
    if (IsTel) { \
        values[Key] = data.Name; \
    }
    SENSOR_DATA_MAP(XX_TO_TB)
#undef XX_TO_TB
}

// 2. Function to convert SensorData to SD Card JSON format
// Flat structure with configurable fields (IsSd)
inline void sensorDataToSd(const SensorData &data, JsonObject &root) {
#define XX_TO_SD(Type, Name, Key, IsTel, IsSd) \
    if (IsSd) { \
        root[Key] = data.Name; \
    }
    SENSOR_DATA_MAP(XX_TO_SD)
#undef XX_TO_SD
}
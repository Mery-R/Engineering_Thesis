#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Error codes
#define ERR_GPS_NO_FIX   (1 << 0)
#define ERR_TEMP_FAIL    (1 << 1)
#define ERR_SD_FAIL      (1 << 2)
#define ERR_TB_FAIL      (1 << 3)

struct SensorData {
  // Timestamp
  uint64_t timestamp;
  int timestamp_time_source;
  // GPS Data
  double lat;
  double lon;
  double alt;
  double speed;
  // Temp Data
  float temp;
  // CAN Data
  float can_speed;
  // Debug Timestamps 
  uint64_t last_gps_fix_timestamp;
  uint64_t last_temp_read_timestamp;
  uint64_t last_can_read_timestamp;
  // Error Code
  uint8_t error_code;
  // Flags
  bool tb_sent;
};

inline void sensorDataToJson(const SensorData &data, JsonObject &out) {
    out["timestamp"] = data.timestamp;
    out["time_source"] = data.timestamp_time_source;
    out["tb_sent"] = data.tb_sent;
    out["error_code"] = data.error_code;
    out["lat"] = data.lat;
    out["lon"] = data.lon;
    out["alt"] = data.alt;
    out["speed"] = data.speed;
    out["temp"] = data.temp;
    out["can_speed"] = data.can_speed;
    out["last_gps_fix_timestamp"] = data.last_gps_fix_timestamp;
    out["last_temp_read_timestamp"] = data.last_temp_read_timestamp;
    out["last_can_read_timestamp"] = data.last_can_read_timestamp;
}

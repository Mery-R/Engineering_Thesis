#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Error codes
#define ERR_GPS_NO_FIX   (1 << 0)
#define ERR_TEMP_FAIL    (1 << 1)
#define ERR_SD_FAIL      (1 << 2)
#define ERR_TB_FAIL      (1 << 3)

struct SensorData {
  double lat;
  double lon;
  double elevation;
  double speed;
  float temp;
  uint64_t timestamp;
  int timestamp_time_source;
  uint64_t last_gps_fix_timestamp;
  uint64_t last_temp_read_timestamp;
  uint8_t error_code;
  bool tb_sent;
};

inline void sensorDataToJson(const SensorData &data, JsonObject &out) {
    out["lat"] = data.lat;
    out["lon"] = data.lon;
    out["elevation"] = data.elevation;
    out["speed"] = data.speed;
    out["temp"] = data.temp;
    out["timestamp"] = data.timestamp;
    out["time_source"] = data.timestamp_time_source;
    out["last_gps_fix_timestamp"] = data.last_gps_fix_timestamp;
    out["last_temp_read_timestamp"] = data.last_temp_read_timestamp;
    out["error_code"] = data.error_code;
    out["tb_sent"] = data.tb_sent;
}

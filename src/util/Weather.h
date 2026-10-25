// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include <I18n.h>

#include <cstdint>
#include <ctime>
#include <string>

// Current weather, kept on the SD card so any connection can refresh it:
//   /weather/now.json       the latest reading, written by the device over Wi-Fi
//                           or by the browser app over USB (same format)
//   /weather/location.json  {"mode":"auto"} = locate by the network's public IP,
//                           or {"mode":"manual","name":..,"lat":..,"lon":..}
// Data comes from Open-Meteo (CC BY 4.0, credited on screen); the automatic
// location from ipwho.is, falling back to geojs.io.
namespace weather {

struct Data {
  std::string city;
  time_t updated = 0;
  int code = -1;  // WMO weather code
  bool day = true;
  float temp = 0, feels = 0, tmax = 0, tmin = 0;
  int humidity = 0;
  float windKmh = 0;
  int windDir = 0;  // degrees the wind blows from
  float uvMax = 0;  // today's maximum
  std::string sunrise, sunset;  // local "HH:MM"
  std::string date;             // the forecast's local date, "YYYY-MM-DD"
};

bool load(Data& out);

// Fetches and saves the current weather over the joined Wi-Fi. Blocks for a few
// seconds; returns false (keeping the old reading) when offline or low on heap.
bool refreshOverWifi();

StrId conditionText(int code);
// 96x96 icon for the condition, or nullptr for an unknown code.
const uint8_t* conditionIcon(int code, bool day);
StrId windDirection(int degrees);
int beaufort(float kmh);
StrId uvLevel(float uv);

}  // namespace weather

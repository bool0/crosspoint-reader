// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include "Weather.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "components/icons/weatherLarge.h"
#include "network/HttpDownloader.h"

namespace weather {
namespace {
constexpr char DIR[] = "/weather";
constexpr char NOW_PATH[] = "/weather/now.json";
constexpr char LOCATION_PATH[] = "/weather/location.json";
constexpr size_t MAX_RESPONSE = 4096;

// Fetches a small JSON body into `out`, capped so a bad server cannot exhaust the heap.
bool fetchJson(const std::string& url, std::string& out) {
  out.clear();
  out.reserve(1024);
  const bool ok = HttpDownloader::fetchUrl(url, [&out](const uint8_t* data, size_t len) {
    if (out.size() + len > MAX_RESPONSE) return false;
    out.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (!ok) LOG_ERR("WTHR", "Fetch failed: %s", url.c_str());
  return ok;
}

struct Place {
  std::string name;
  double lat = 0, lon = 0;
};

bool manualPlace(Place& place) {
  std::string raw;
  if (!Storage.readFileToString("WTHR", LOCATION_PATH, 1024, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return false;
  if (doc["mode"] != "manual" || !doc["lat"].is<double>() || !doc["lon"].is<double>()) return false;
  place.name = doc["name"] | "";
  place.lat = doc["lat"];
  place.lon = doc["lon"];
  return true;
}

// Free IP geolocation services, tried in order: each rate-limits or goes down now and then.
constexpr const char* IP_LOOKUPS[] = {
    "https://ipwho.is/?fields=city,latitude,longitude",
    "https://get.geojs.io/v1/ip/geo.json",
};

// geojs sends coordinates as strings, the others as numbers.
double number(const JsonVariantConst v) {
  return v.is<const char*>() ? strtod(v.as<const char*>(), nullptr) : v.as<double>();
}

bool placeFromIp(Place& place) {
  // ipwho.is names the city in Chinese when the device is set to Chinese.
  const bool chinese = I18N.getLanguage() == Language::ZH;
  for (const char* lookup : IP_LOOKUPS) {
    std::string url = lookup;
    if (chinese && url.find("ipwho.is") != std::string::npos) url += "&lang=zh-CN";
    std::string raw;
    JsonDocument doc;
    if (!fetchJson(url, raw) || deserializeJson(doc, raw) != DeserializationError::Ok) continue;
    place.lat = number(doc["latitude"]);
    place.lon = number(doc["longitude"]);
    if (place.lat == 0 && place.lon == 0) continue;
    place.name = doc["city"] | "";
    return true;
  }
  return false;
}

// "2026-10-25T06:02" -> "06:02".
std::string clockPart(const char* iso) {
  const std::string s = iso ? iso : "";
  const size_t t = s.find('T');
  return t == std::string::npos ? s : s.substr(t + 1, 5);
}
}  // namespace

bool load(Data& out) {
  std::string raw;
  if (!Storage.readFileToString("WTHR", NOW_PATH, 2048, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok || !doc["code"].is<int>()) return false;
  out.city = doc["city"] | "";
  out.updated = doc["updated"] | 0;
  out.code = doc["code"];
  out.day = (doc["day"] | 1) != 0;
  out.temp = doc["temp"] | 0.0f;
  out.feels = doc["feels"] | 0.0f;
  out.tmax = doc["tmax"] | 0.0f;
  out.tmin = doc["tmin"] | 0.0f;
  out.humidity = doc["humidity"] | 0;
  out.windKmh = doc["wind"] | 0.0f;
  out.windDir = doc["windDir"] | 0;
  out.uvMax = doc["uv"] | 0.0f;
  out.sunrise = doc["sunrise"] | "";
  out.sunset = doc["sunset"] | "";
  out.date = doc["date"] | "";
  return true;
}

bool refreshOverWifi() {
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR("WTHR", "Not enough heap for TLS (%u free)", ESP.getFreeHeap());
    return false;
  }
  Place place;
  if (!manualPlace(place) && !placeFromIp(place)) return false;

  char url[384];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,"
           "wind_speed_10m,wind_direction_10m"
           "&daily=sunrise,sunset,temperature_2m_max,temperature_2m_min,uv_index_max&timezone=auto&forecast_days=1",
           place.lat, place.lon);
  std::string raw;
  if (!fetchJson(url, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok || !doc["current"]["weather_code"].is<int>()) {
    LOG_ERR("WTHR", "Unexpected forecast reply");
    return false;
  }
  const JsonObject cur = doc["current"];
  const JsonObject daily = doc["daily"];

  JsonDocument now;
  now["city"] = place.name;
  now["updated"] = static_cast<long long>(time(nullptr));
  now["code"] = cur["weather_code"];
  now["day"] = cur["is_day"];
  now["temp"] = cur["temperature_2m"];
  now["feels"] = cur["apparent_temperature"];
  now["humidity"] = cur["relative_humidity_2m"];
  now["wind"] = cur["wind_speed_10m"];
  now["windDir"] = cur["wind_direction_10m"];
  now["uv"] = daily["uv_index_max"][0];
  now["tmax"] = daily["temperature_2m_max"][0];
  now["tmin"] = daily["temperature_2m_min"][0];
  now["sunrise"] = clockPart(daily["sunrise"][0]);
  now["sunset"] = clockPart(daily["sunset"][0]);
  now["date"] = daily["time"][0];
  String out;
  serializeJson(now, out);
  Storage.mkdir(DIR);
  if (!Storage.writeFile(NOW_PATH, out)) return false;
  LOG_INF("WTHR", "Updated for %s", place.name.c_str());
  return true;
}

StrId conditionText(const int code) {
  switch (code) {
    case 0:
      return StrId::STR_WX_CLEAR;
    case 1:
      return StrId::STR_WX_MAINLY_CLEAR;
    case 2:
      return StrId::STR_WX_PARTLY_CLOUDY;
    case 3:
      return StrId::STR_WX_OVERCAST;
    case 45:
    case 48:
      return StrId::STR_WX_FOG;
    case 51:
    case 53:
    case 55:
      return StrId::STR_WX_DRIZZLE;
    case 56:
    case 57:
    case 66:
    case 67:
      return StrId::STR_WX_FREEZING_RAIN;
    case 61:
      return StrId::STR_WX_LIGHT_RAIN;
    case 63:
      return StrId::STR_WX_RAIN;
    case 65:
      return StrId::STR_WX_HEAVY_RAIN;
    case 71:
      return StrId::STR_WX_LIGHT_SNOW;
    case 73:
    case 77:
      return StrId::STR_WX_SNOW;
    case 75:
      return StrId::STR_WX_HEAVY_SNOW;
    case 80:
    case 81:
      return StrId::STR_WX_SHOWERS;
    case 82:
      return StrId::STR_WX_HEAVY_SHOWERS;
    case 85:
    case 86:
      return StrId::STR_WX_SNOW_SHOWERS;
    case 95:
      return StrId::STR_WX_THUNDER;
    case 96:
    case 99:
      return StrId::STR_WX_THUNDER_HAIL;
    default:
      return StrId::STR_WX_UNKNOWN;
  }
}

const uint8_t* conditionIcon(const int code, const bool day) {
  if (code == 0 || code == 1) return day ? WeatherSunIcon : WeatherMoonIcon;
  if (code == 2) return day ? WeatherCloudSunIcon : WeatherCloudMoonIcon;
  if (code == 3) return WeatherCloudIcon;
  if (code == 45 || code == 48) return WeatherFogIcon;
  if (code >= 51 && code <= 57) return WeatherDrizzleIcon;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return WeatherRainIcon;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WeatherSnowIcon;
  if (code >= 95) return WeatherStormIcon;
  return nullptr;
}

StrId windDirection(const int degrees) {
  static constexpr StrId DIRS[8] = {StrId::STR_WIND_N, StrId::STR_WIND_NE, StrId::STR_WIND_E, StrId::STR_WIND_SE,
                                    StrId::STR_WIND_S, StrId::STR_WIND_SW, StrId::STR_WIND_W, StrId::STR_WIND_NW};
  return DIRS[static_cast<int>(std::lround(((degrees % 360) + 360) % 360 / 45.0)) % 8];
}

int beaufort(const float kmh) {
  static constexpr float LIMITS[12] = {1, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118};
  int force = 0;
  while (force < 12 && kmh >= LIMITS[force]) force++;
  return force;
}

StrId uvLevel(const float uv) {
  if (uv < 3) return StrId::STR_UV_LOW;
  if (uv < 6) return StrId::STR_UV_MODERATE;
  if (uv < 8) return StrId::STR_UV_HIGH;
  if (uv < 11) return StrId::STR_UV_VERY_HIGH;
  return StrId::STR_UV_EXTREME;
}

}  // namespace weather

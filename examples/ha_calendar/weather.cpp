#include "weather.h"

#include "config.h"
#include "ha_client.h"
#include "types.h"
#include <ArduinoJson.h>

extern JsonDocument haDoc;
void disableWiFiIfAllowed();

bool fetchWeather(WeatherData &currentWeather) {
  Serial.println("=== fetchWeather() called ===");
  String url = buildHaUrl(String("/api/states/") + String(ENTITY_WEATHER));
  Serial.print("Weather URL: ");
  Serial.println(url);

  haDoc.clear();

  if (!fetchJson(url, haDoc)) {
    Serial.println("ERROR: Weather fetch failed - fetchJson returned false");
    return false;
  }

  Serial.println("Weather JSON fetched successfully");

  const char *state = haDoc["state"];
  WeatherData newWeather;

  // Check if temperature exists and is valid
  if (!haDoc["attributes"]["temperature"].is<float>()) {
    Serial.println("WARNING: Temperature not found or invalid in JSON");
    newWeather.temperature = "-- C";
  } else {
    float temp = haDoc["attributes"]["temperature"];
    float displayTemp = temp;
    const char *unit = " C";
    if (USE_FAHRENHEIT) {
      displayTemp = temp * 9.0 / 5.0 + 32.0;
      unit = " F";
    }
    newWeather.temperature = String(displayTemp, 1) + unit;
  }

  newWeather.condition = state ? String(state) : "--";

  bool changed = (newWeather.condition != currentWeather.condition ||
                  newWeather.temperature != currentWeather.temperature);
  if (changed) {
    currentWeather = newWeather;
  }

  Serial.print("Weather condition: ");
  Serial.println(currentWeather.condition);
  Serial.print("Weather temperature: ");
  Serial.println(currentWeather.temperature);
  Serial.println("=== fetchWeather() complete ===");
  disableWiFiIfAllowed();
  return changed;
}

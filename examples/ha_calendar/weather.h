#pragma once

#include "types.h"

// Fetch weather from Home Assistant into currentWeather.
// Returns true when the visible display data changed.
bool fetchWeather(WeatherData &currentWeather);

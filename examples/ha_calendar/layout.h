#pragma once

#include <stdint.h>
#include "epd_driver.h"

// ---------- Update Intervals ----------
const unsigned long WEATHER_UPDATE_INTERVAL_MS   = 60UL * 60UL * 1000UL;        // 1 hour
const unsigned long CAL_TODO_UPDATE_INTERVAL_MS   = 6UL * 60UL * 60UL * 1000UL; // 6 hours
const unsigned long QUOTE_ROTATION_INTERVAL_MS    = 6UL * 60UL * 60UL * 1000UL; // 6 hours
const unsigned long QUOTE_FETCH_INTERVAL_MS       = 24UL * 60UL * 60UL * 1000UL;// 24 hours
const unsigned long MIDNIGHT_CHECK_INTERVAL_MS    = 60UL * 1000UL;               // 1 minute
const unsigned long OTA_WINDOW_MS                 = 5UL * 60UL * 1000UL;         // 5 minutes
const unsigned long BATTERY_UPDATE_INTERVAL_MS    = 10UL * 60UL * 1000UL;        // 10 minutes

// ---------- Screen Layout (960x540) ----------
// Top Header
const Rect_t clockArea          = {.x = 20,  .y = 20,  .width = 120, .height = 35};
const Rect_t weatherArea        = {.x = 820, .y = 20,  .width = 120, .height = 35};
const Rect_t quoteArea          = {.x = 20,  .y = 60,  .width = 920, .height = 70};

// Middle Section - Todo (left) and Calendar (right)
const Rect_t todoHeaderArea     = {.x = 20,  .y = 135, .width = 450, .height = 40};
const Rect_t todoListArea       = {.x = 20,  .y = 175, .width = 450, .height = 345};
const Rect_t calendarHeaderArea = {.x = 490, .y = 135, .width = 450, .height = 40};
const Rect_t calendarListArea   = {.x = 490, .y = 175, .width = 450, .height = 345};

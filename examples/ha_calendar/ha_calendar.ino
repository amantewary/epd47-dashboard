/**
 * Home Assistant Dashboard for LilyGo EPD47
 *
 * A comprehensive e-paper dashboard that displays:
 * - Date (top-left)
 * - Weather information with icons
 * - Daily motivational quotes (rotated every 6 hours)
 * - Todo list items (due today)
 * - Upcoming calendar events (next 7 days)
 * - Battery voltage logged (not drawn)
 *
 * Features:
 * - Partial refresh for fast updates and minimal flashing
 * - OTA (Over-The-Air) updates via WiFi; WiFi disabled between fetches unless OTA window is active
 * - Optimized refresh rates (hourly weather, 6-hour quotes, 6-hour todo/calendar, midnight full refresh)
 * - On-demand OTA window opened by BUTTON_1 (GPIO21) for 5 minutes
 *
 * Hardware: LilyGo T5-ePaper-S3 (ESP32-S3, 4.7" EPD, 960x540)
 * Framework: Arduino/PlatformIO
 */

#ifndef BOARD_HAS_PSRAM
#error "Please enable PSRAM, Arduino IDE -> Tools -> PSRAM -> OPI PSRAM"
#endif

#include "calendar_icons.h"
#include "config.h" // Entity configuration (not tracked by git)
#include "epd_driver.h"
#include "esp_adc_cal.h" // For ADC calibration
#include "esp_sleep.h"
#include "firasans.h"
#include "firasans_medium.h"
#include "firasans_small.h"
#include "calendar.h"
#include "display.h"
#include "ha_client.h"
#include "quotes.h"
#include "secrets.h"
#include "types.h"
#include "todo_icons.h"
#include "utilities.h"
#include "weather_icons.h"
#include "weather.h"
#include "todos.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <algorithm>
#include <vector>

// Shared JSON documents for Home Assistant responses
// With ArduinoJson v7, JsonDocument manages capacity dynamically.
JsonDocument haDoc;       // Single-object responses (/api/states, service responses)
JsonDocument haArrayDoc;  // Larger array responses (calendar, quotes)

// ---------- CONFIG ----------

// OTA password (keep private in secrets.h)
#ifndef OTA_PASSWORD_VALUE
#define OTA_PASSWORD_VALUE "CHANGE_ME_OTA_PASSWORD"
#endif
const char *OTA_PASSWORD = OTA_PASSWORD_VALUE;

// HA entities (loaded from config.h, which is not committed to git)
// These macros are used directly in the code - no need for const char*
// variables

// NTP Config (loaded from config.h)
const char *ntpServer = NTP_SERVER;
const long gmtOffset_sec = GMT_OFFSET_SEC;
const int daylightOffset_sec = DAYLIGHT_OFFSET_SEC;

// Build todo entities vector from config.h defines
#if ENTITY_TODOS_COUNT > 10
#error "ENTITY_TODOS_COUNT supports up to 10 todos; extend the vector initialization if you need more."
#endif
extern const std::vector<const char *> ENTITY_TODOS = {
#if ENTITY_TODOS_COUNT >= 1
    ENTITY_TODO_1,
#endif
#if ENTITY_TODOS_COUNT >= 2
    ENTITY_TODO_2,
#endif
#if ENTITY_TODOS_COUNT >= 3
    ENTITY_TODO_3,
#endif
#if ENTITY_TODOS_COUNT >= 4
    ENTITY_TODO_4,
#endif
#if ENTITY_TODOS_COUNT >= 5
    ENTITY_TODO_5,
#endif
#if ENTITY_TODOS_COUNT >= 6
    ENTITY_TODO_6,
#endif
#if ENTITY_TODOS_COUNT >= 7
    ENTITY_TODO_7,
#endif
#if ENTITY_TODOS_COUNT >= 8
    ENTITY_TODO_8,
#endif
#if ENTITY_TODOS_COUNT >= 9
    ENTITY_TODO_9,
#endif
#if ENTITY_TODOS_COUNT >= 10
    ENTITY_TODO_10,
#endif
};

// Build calendar entities vector from config.h defines
#if ENTITY_CALENDARS_COUNT > 10
#error "ENTITY_CALENDARS_COUNT supports up to 10 calendars; extend the vector initialization if you need more."
#endif
extern const std::vector<const char *> ENTITY_CALENDARS = {
#if ENTITY_CALENDARS_COUNT >= 1
    ENTITY_CALENDAR_1,
#endif
#if ENTITY_CALENDARS_COUNT >= 2
    ENTITY_CALENDAR_2,
#endif
#if ENTITY_CALENDARS_COUNT >= 3
    ENTITY_CALENDAR_3,
#endif
#if ENTITY_CALENDARS_COUNT >= 4
    ENTITY_CALENDAR_4,
#endif
#if ENTITY_CALENDARS_COUNT >= 5
    ENTITY_CALENDAR_5,
#endif
#if ENTITY_CALENDARS_COUNT >= 6
    ENTITY_CALENDAR_6,
#endif
#if ENTITY_CALENDARS_COUNT >= 7
    ENTITY_CALENDAR_7,
#endif
#if ENTITY_CALENDARS_COUNT >= 8
    ENTITY_CALENDAR_8,
#endif
#if ENTITY_CALENDARS_COUNT >= 9
    ENTITY_CALENDAR_9,
#endif
#if ENTITY_CALENDARS_COUNT >= 10
    ENTITY_CALENDAR_10,
#endif
};

// Update intervals (ms)
const unsigned long WEATHER_UPDATE_INTERVAL_MS = 60UL * 60UL * 1000UL; // 1 hour
const unsigned long CAL_TODO_UPDATE_INTERVAL_MS =
    6UL * 60UL * 60UL * 1000UL; // 6 hours
const unsigned long QUOTE_ROTATION_INTERVAL_MS =
    6UL * 60UL * 60UL * 1000UL; // 6 hours
const unsigned long QUOTE_FETCH_INTERVAL_MS =
    24UL * 60UL * 60UL * 1000UL; // 24 hours (fetch new quotes once per day)
const unsigned long MIDNIGHT_CHECK_INTERVAL_MS = 60UL * 1000UL; // check once per minute
const unsigned long OTA_WINDOW_MS = 5UL * 60UL * 1000UL; // OTA enabled for 5 minutes after button press

// ---------- Data Structures ----------
// Core data types are defined in types.h
BatteryData batteryInfo = {0.0, 0};
const unsigned long BATTERY_UPDATE_INTERVAL_MS =
    10UL * 60UL * 1000UL; // Update every 10 minutes
int vref = 1100;   // Reference voltage in mV (will be calibrated from eFuse if
                   // available)

// Global Data
WeatherData currentWeather;
std::vector<TodoItem> todoList;
std::vector<CalendarEvent> calendarEvents;
std::vector<QuoteData> quotes;
int currentQuoteIndex = 0;

// Initialize STL collections with reasonable reserved capacity to reduce heap fragmentation
void initCollections() {
  todoList.reserve(8);          // Tasks due today are usually small in number
  calendarEvents.reserve(16);   // A week of events across calendars
  quotes.reserve(64);           // Daily quotes cache
}

// ---------- Layout ----------
// Screen is 960x540
// Layout:
//   ┌──────────────────────────────────────┐
//   │ DATE (left)             WEATHER (right) │
//   │ "Quote of the day..."                  │
//   ├────────────────┬───────────────────────┤
//   │   TODO LIST    │      UPCOMING         │
//   │   □ Task 1     │   1/15 10:00          │
//   │   □ Task 2     │     Meeting title...  │
//   │   ☑ Task 3     │   ...                 │
//   └────────────────┴───────────────────────┘

// Top Header - Date and Weather (Screen: 960x540)
const Rect_t clockArea = {.x = 20,
                          .y = 20,
                          .width = 120,
                          .height = 35}; // Used for date display (clock removed)
const Rect_t weatherArea = {.x = 820,
                            .y = 20,
                            .width = 120,
                            .height = 35}; // Weather in top right
const Rect_t quoteArea = {
    .x = 20,
    .y = 60,
    .width = 920,
    .height = 70}; // Full width for daily quote (quote only, no author)

// Middle Section - Todo (left half) and UPCOMING Calendar (right half) side by
// side. Content spans roughly y=135..520 for tighter vertical fit.
const Rect_t todoHeaderArea = {
    .x = 20, .y = 135, .width = 450, .height = 40}; // Left half
const Rect_t todoListArea = {
    .x = 20,
    .y = 175,
    .width = 450,
    .height = 345}; // Left half - ends near y=520
const Rect_t calendarHeaderArea = {
    .x = 490, .y = 135, .width = 450, .height = 40}; // Right half
const Rect_t calendarListArea = {
    .x = 490,
    .y = 175,
    .width = 450,
    .height = 345}; // Right half - ends near y=520

// ---------- Globals ----------
unsigned long lastWeatherUpdate = 0;
unsigned long lastCalTodoUpdate = 0;
unsigned long lastQuoteFetch = 0;
unsigned long lastQuoteRotation = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastMidnightCheck = 0;
String currentDateString = ""; // Track current date for logging and comparisons
bool fullRefreshScheduled = false;
int lastMidnightDay = -1;
bool otaEnabled = false;
unsigned long otaWindowEnds = 0;
int lastButtonState = HIGH;
unsigned long wifiLingerUntil = 0; // keep WiFi up briefly after fetches

bool debugMode = false; // true when BUTTON_1 held at boot, keeps device awake for OTA/debug

static void redrawQuoteSection() {
  epd_poweron();
  drawQuote(quotes, currentQuoteIndex, quoteArea);
  epd_poweroff();
}

void forceWifiOff() {
  Serial.println("Forcing WiFi off");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiLingerUntil = 0;
}

// Simple connectivity helpers used across modules
bool isWiFiConnected() { return (WiFi.status() == WL_CONNECTED); }

void disableWiFiIfAllowed() {
  // In debug mode, keep WiFi on so OTA and live debugging remain available
  if (debugMode)
    return;
  if (otaEnabled)
    return;
  if (millis() < wifiLingerUntil)
    return;
  forceWifiOff();
}

void enableOtaWindow() {
  otaEnabled = true;
  otaWindowEnds = millis() + OTA_WINDOW_MS;
  Serial.println("OTA window enabled");
}

/**
 * Read battery voltage and calculate a rough percentage.
 * Uses the ADC directly and does not depend on EPD power state.
 *
 * @return BatteryData struct with voltage and percentage.
 */
BatteryData readBattery() {
  BatteryData bat;

  // Read ADC value (0-4095 for 12-bit ADC)
  uint16_t adcValue = analogRead(BATT_PIN);

  // Debug: Print raw ADC value
  Serial.print("  Raw ADC value: ");
  Serial.println(adcValue);

  // Calculate voltage: ADC value / 4095 * 2.0 (voltage divider) * 3.3V *
  // (vref/1000) Formula from demo example: ((float)v / 4095.0) * 2.0 * 3.3 *
  // (vref / 1000.0)
  float voltage = ((float)adcValue / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);

  // Debug: Print calculated voltage before clamping
  Serial.print("  Calculated voltage (before clamp): ");
  Serial.print(voltage, 3);
  Serial.println("V");

  // Clamp to max 4.2V (fully charged LiPo)
  if (voltage >= 4.2) {
    voltage = 4.2;
  }

  // Check for invalid readings (too low or zero)
  if (voltage < 0.5) {
    Serial.println("  ⚠ WARNING: Battery voltage very low (< 0.5V) - Check "
                   "battery connection!");
    Serial.println("  ⚠ Possible issues:");
    Serial.println("     - Battery not connected");
    Serial.println("     - Battery completely discharged");
    Serial.println("     - ADC pin issue");
  }

  bat.voltage = voltage;

  // Calculate percentage using a more realistic range:
  // ~3.3V  = 0%
  // ~4.15V = 100%
  //
  // Note:
  // - Many LiPo packs sit near 4.2V while still charging (constant voltage
  //   phase), so mapping 4.2V directly to 100% tends to show "100%" too early.
  // - Using 3.3–4.15V gives a more useful spread of percentages in normal use.
  float minVoltage = 3.3;  // Treat ~3.3V as effectively empty for display purposes
  float maxVoltage = 4.15; // Treat ~4.15V as "full" for percentage calculation

  // Only calculate percentage if voltage is reasonable
  if (voltage >= minVoltage) {
    bat.percentage =
        (int)(((voltage - minVoltage) / (maxVoltage - minVoltage)) * 100.0);
    bat.percentage = constrain(bat.percentage, 0, 100);
  } else if (voltage > 0.5) {
    // Voltage between 0.5V and 3.0V - battery is very low but connected
    // Show a small percentage instead of 0% to indicate battery is present
    bat.percentage =
        (int)((voltage / minVoltage) * 5.0); // Show 0-5% for very low battery
    Serial.print("  ⚠ Battery voltage very low (");
    Serial.print(voltage, 3);
    Serial.print("V) - Battery needs charging!");
  } else {
    // Voltage below 0.5V - battery likely not connected or completely dead
    bat.percentage = 0;
    Serial.print("  ⚠ Voltage (");
    Serial.print(voltage, 3);
    Serial.print("V) below minimum (");
    Serial.print(minVoltage);
    Serial.println("V) - Check battery connection!");
  }

  // Charging detection no longer displayed; percentage is based solely on voltage.
  return bat;
}



// Parse ISO datetime string to time_t
time_t parseISODateTime(const String &isoStr) {
  if (isoStr.length() < 10)
    return 0;

  // Work on a trimmed copy that strips timezone information (Z, +HH:MM, -HH:MM)
  String trimmed = isoStr;

  int tzPos = trimmed.indexOf('Z');
  if (tzPos == -1) {
    // Look for '+' or '-' only after the date portion (index > 10),
    // to avoid matching the '-' in "YYYY-MM-DD"
    for (int i = 10; i < (int)trimmed.length(); ++i) {
      char c = trimmed.charAt(i);
      if (c == '+' || c == '-') {
        tzPos = i;
        break;
      }
    }
  }
  if (tzPos > 10) {
    trimmed = trimmed.substring(0, tzPos);
  }

  struct tm timeinfo = {0};
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;

  // First try full datetime: YYYY-MM-DDTHH:MM:SS
  int parsed = sscanf(trimmed.c_str(), "%d-%d-%dT%d:%d:%d",
                      &year, &month, &day, &hour, &minute, &second);
  if (parsed < 3) {
    // Fallback to date-only: YYYY-MM-DD
    hour = minute = second = 0;
    parsed = sscanf(trimmed.c_str(), "%d-%d-%d", &year, &month, &day);
  }

  if (parsed >= 3) {
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = month - 1;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    return mktime(&timeinfo);
  }

  return 0;
}

// Calculate time until next calendar event (returns minutes, -1 if no events)
int getMinutesUntilNextEvent() {
  if (calendarEvents.empty())
    return -1;

  time_t now;
  time(&now);

  // Find the next event in the future
  for (const auto &evt : calendarEvents) {
    time_t eventTime = parseISODateTime(evt.isoDateTime);
    if (eventTime > now) {
      int diff = (int)((eventTime - now) / 60); // Convert to minutes
      return diff;
    }
  }

  return -1; // No future events
}


// Update weather section only - compact display for top center with icon
void updateWeatherSection(bool powerOn = true) {
  Serial.println("Updating Weather Section...");
  if (powerOn) {
    epd_poweron();
  }
  epd_clear_area(weatherArea);

  // Draw weather icon on the left (smaller size to fit compact area)
  String condition = currentWeather.condition;
  int32_t icon_x = weatherArea.x + 2;
  int32_t icon_y =
      weatherArea.y - 6; // Slightly above to center better in 35px height

  // Use smaller icon size - draw 32x32 from 48x48 icon (centered)
  if (condition.indexOf("sun") >= 0 || condition.indexOf("clear") >= 0) {
    // Draw sun icon - use a smaller area (32x32) from the 48x48 icon
    Rect_t icon_area = {.x = icon_x, .y = icon_y, .width = 32, .height = 32};
    // We'll draw the full icon but it will be clipped to the area
    drawBitmapIcon(icon_x, icon_y, icon_sun_data, icon_sun_width,
                   icon_sun_height);
  } else {
    // Draw cloud icon
    drawBitmapIcon(icon_x, icon_y, icon_cloud_data, icon_cloud_width,
                   icon_cloud_height);
  }

  // Compact display: show temperature next to icon (e.g., "22°C" or "72°F")
  // Extract just the number from temperature string (remove " C" or " F")
  String tempDisplay = currentWeather.temperature;
  // Remove trailing space and unit if present, we'll add our own
  tempDisplay.trim();
  if (tempDisplay.endsWith(" C")) {
    tempDisplay = tempDisplay.substring(0, tempDisplay.length() - 2) + "°C";
  } else if (tempDisplay.endsWith(" F")) {
    tempDisplay = tempDisplay.substring(0, tempDisplay.length() - 2) + "°F";
  }

  // Draw temperature text next to icon (starting after icon + larger gap) -
  // medium font
  int32_t weather_x =
      weatherArea.x +
      50; // Start after icon (48px) + 2px gap for better spacing
  int32_t weather_y =
      weatherArea.y + 25; // Same offset as clock/date for alignment
  writeln((GFXfont *)&FiraSansMedium, tempDisplay.c_str(), &weather_x,
          &weather_y, NULL);

  if (powerOn) {
    epd_poweroff();
  }
}

// Update calendar and todo sections only
void updateCalendarTodoSections() {
  Serial.println("Updating Calendar and Todo Sections...");
  epd_poweron();

  // Update Todo List
  std::vector<String> todoLines;
  todoLines.reserve(todoList.size() + 1);
  for (const auto &item : todoList) {
    // Overdue items are prefixed with "!" and include due date (MM-DD)
    String prefix = item.overdue ? "! " : "> ";
    String line = prefix + item.text;
    if (item.overdue && item.dueDate.length() >= 5) {
      line += " (Due " + item.dueDate.substring(5) + ")";
    }
    todoLines.push_back(line);
  }

  if (todoLines.empty()) {
    todoLines.push_back("> Nothing due today");
  }

  drawList(todoListArea, todoLines, "TODO", true, 35, icon_todo_data,
           icon_todo_width, icon_todo_height);

  // Update Calendar Events
  std::vector<String> calLines;
  calLines.reserve(calendarEvents.size() * 2 + 1); // two lines per event + fallback
  for (const auto &evt : calendarEvents) {
    String eventLine = evt.date + " " + evt.startTime + " - " + evt.title;
    if (eventLine.length() > 90) {
      eventLine = eventLine.substring(0, 87) + "...";
    }
    calLines.push_back(eventLine);
  }

  if (calLines.empty()) {
    calLines.push_back("No upcoming events");
  }

  // Use tighter line spacing (28) for calendar to fit more events
  drawList(calendarListArea, calLines, "UPCOMING", false, 28,
           icon_calendar_data, icon_calendar_width, icon_calendar_height);

  epd_poweroff();
}

void drawDashboard() {
  Serial.println("=== Starting drawDashboard ===");
  epd_poweron();
  epd_clear();

  Serial.println("Screen cleared, drawing content...");

  // ========== TOP HEADER ==========
  // Date (placed at former clock position to reduce refresh frequency)
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char dateStr[50];
  strftime(dateStr, sizeof(dateStr), "%a %b %d",
           &timeinfo); // "Mon Jan 15"
  currentDateString = String(dateStr);

  int32_t date_x = clockArea.x;
  int32_t date_y = clockArea.y + 25; // Use compact positioning
  epd_clear_area(clockArea);
  writeln((GFXfont *)&FiraSansMedium, dateStr, &date_x, &date_y, NULL);

  // Weather (Top Center) - Compact temperature display
  updateWeatherSection(false);

  // ========== QUOTE SECTION ==========
  drawQuote(quotes, currentQuoteIndex, quoteArea);

  // Draw divider below quote section
  // Divider below quote
  drawTextDivider(quoteArea.x, quoteArea.y + quoteArea.height - 5,
                  quoteArea.width);

  // ========== MIDDLE SECTION ==========

  // 4. Todo List (Left Half)
  std::vector<String> todoLines;
  todoLines.reserve(todoList.size() + 1);
  for (const auto &item : todoList) {
    // Overdue items are prefixed with "!" and include due date (MM-DD)
    String prefix = item.overdue ? "! " : "> ";
    String line = prefix + item.text;
    if (item.overdue && item.dueDate.length() >= 5) {
      line += " (Due " + item.dueDate.substring(5) + ")";
    }
    if (line.length() > 45) { // Reduced for half width
      line = line.substring(0, 42) + "...";
    }
    todoLines.push_back(line);
  }

  if (todoLines.empty()) {
    todoLines.push_back("> Nothing due today");
  }

  drawList(todoListArea, todoLines, "TODO", true, 32, icon_todo_data,
           icon_todo_width, icon_todo_height);

  // 5. UPCOMING Calendar Events (Right Half) - Side by side with TODO
  // Format: Date/Time on first line (compact), Title on second line
  Serial.print("=== Building calendar display lines from ");
  Serial.print(calendarEvents.size());
  Serial.println(" events ===");

  std::vector<String> calLines;
  calLines.reserve(calendarEvents.size() * 2 + 1); // two lines per event + fallback
  int eventIndex = 0;
  for (const auto &evt : calendarEvents) {
    Serial.print("Processing event #");
    Serial.print(eventIndex);
    Serial.print(": ");
    Serial.print(evt.title);
    Serial.print(" (");
    Serial.print(evt.isoDateTime);
    Serial.println(")");

    // First line: compact date and time format
    // Convert "01-15" to "1/15" and "14:30" to "2:30 PM" or just "2:30"
    String compactDate = evt.date;
    compactDate.replace("-", "/");
    // Remove leading zeros from month/day
    int dashPos = compactDate.indexOf('/');
    if (dashPos > 0) {
      String month = compactDate.substring(0, dashPos);
      String day = compactDate.substring(dashPos + 1);
      if (month.startsWith("0") && month.length() > 1)
        month = month.substring(1);
      if (day.startsWith("0") && day.length() > 1)
        day = day.substring(1);
      compactDate = month + "/" + day;
    }

    // Format time more compactly - remove leading zeros from hour
    String compactTime = evt.startTime;
    if (compactTime.indexOf(":") > 0) {
      int colonPos = compactTime.indexOf(":");
      String hour = compactTime.substring(0, colonPos);
      String minute = compactTime.substring(colonPos);
      if (hour.startsWith("0") && hour.length() > 1)
        hour = hour.substring(1);
      compactTime = hour + minute;
    }

    String dateTimeLine = compactDate + " " + compactTime;
    calLines.push_back(dateTimeLine);

    // Second line: title (truncate if needed)
    // Add a marker at the start to help identify this as a title (not
    // date/time)
    String titleLine =
        "  " + evt.title; // Indent with 2 spaces to distinguish from date/time
    int maxTitleLength = 28; // Half-width area
    if (titleLine.length() > maxTitleLength) {
      titleLine = titleLine.substring(0, maxTitleLength - 3) + "...";
    }
    calLines.push_back(titleLine);

    Serial.print("  Added line pair - DateTime: '");
    Serial.print(dateTimeLine);
    Serial.print("' Title: '");
    Serial.print(titleLine);
    Serial.println("'");

    eventIndex++;
  }

  Serial.print("Total lines to display: ");
  Serial.println(calLines.size());
  Serial.print("Calendar list area: x=");
  Serial.print(calendarListArea.x);
  Serial.print(" y=");
  Serial.print(calendarListArea.y);
  Serial.print(" width=");
  Serial.print(calendarListArea.width);
  Serial.print(" height=");
  Serial.println(calendarListArea.height);

  if (calLines.empty()) {
    calLines.push_back("No upcoming events");
  }

  // Use consistent line spacing (32) for calendar (2 lines per event: date/time
  // + title)
  Serial.println(">>> Calling drawList for UPCOMING calendar");
  drawList(calendarListArea, calLines, "UPCOMING", false, 32,
           icon_calendar_data, icon_calendar_width, icon_calendar_height);
  Serial.println(">>> drawList for UPCOMING calendar completed");

  Serial.println("=== drawDashboard complete ===");

  // Power off display to save battery
  // Note: This only powers off the e-paper display, not the ESP32
  // The ESP32 continues running and can be powered by battery or USB-C
  // When USB-C is connected, the board automatically uses USB power and charges
  // the battery
  epd_poweroff();
}

/**
 * Draw initial screen (blank white screen)
 * Called once during setup before connecting to WiFi
 * Works on both battery and USB-C power (automatic switching)
 */
void drawInitialScreen() {
  epd_poweron();
  epd_clear();
  epd_poweroff();
}

/**
 * Update date tracking (no drawing). Called in setup to initialize currentDateString
 * and lastMidnightDay.
 */
void updateDate() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char dateStr[50];
  strftime(dateStr, sizeof(dateStr), "%a %b %d",
           &timeinfo); // "Mon Jan 15"
  String newDateString = String(dateStr);

  currentDateString = newDateString;
  lastMidnightDay = timeinfo.tm_mday;
  Serial.print("Date initialized to ");
  Serial.println(currentDateString);
}

// ---------- Arduino lifecycle ----------
/**
 * Setup function - Initializes display, WiFi, NTP, and fetches initial data
 *
 * Power Management:
 * - Works on battery power (automatic power switching)
 * - Works while charging via USB-C (board auto-switches to USB power)
 * - Display is powered on only when updating (saves battery)
 * - No manual power source switching needed - handled automatically by hardware
 */
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nEPD47 Home Assistant Dashboard");

  pinMode(BUTTON_1, INPUT_PULLUP); // Button to open OTA window on demand
  lastButtonState = digitalRead(BUTTON_1);

  // Decide mode at boot based on BUTTON_1 (held = debug/OTA mode, released = battery-optimized mode)
  bool bootButtonPressed = (lastButtonState == LOW);
  if (bootButtonPressed) {
    debugMode = true;
    Serial.println("Debug mode enabled (BUTTON_1 held at boot)");
  } else {
    debugMode = false;
    Serial.println("Battery-optimized mode (BUTTON_1 not held at boot)");
  }

  if (String(OTA_PASSWORD) == "CHANGE_ME_OTA_PASSWORD") {
    Serial.println("FATAL: OTA password is not set. Update OTA_PASSWORD_VALUE in secrets.h and OTA_PASSWORD in .platformio_env.");
    while (true) {
      delay(1000);
    }
  }
  if (String(OTA_PASSWORD).length() < 8) {
    Serial.println("⚠ WARNING: OTA password is shorter than 8 characters. Consider using a stronger password.");
  }

  // Configure ADC for battery reading (ESP32-S3)
  // BATT_PIN is GPIO 14 for ESP32-S3
  // Note: ADC1 is used for GPIO 0-21 on ESP32-S3
  pinMode(BATT_PIN, INPUT);
  analogReadResolution(12);       // 12-bit resolution (0-4095)
  analogSetAttenuation(ADC_11db); // 11dB attenuation allows 0-3.3V range

  // Calibrate ADC reference voltage from eFuse (more accurate than hardcoded
  // value) This is the same method used in the demo example
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
      ADC_UNIT_1,       // ESP32-S3 battery pin uses ADC1
      ADC_ATTEN_DB_11,  // 11dB attenuation
      ADC_WIDTH_BIT_12, // 12-bit width
      1100,             // Default vref (will be overridden if eFuse available)
      &adc_chars);

  if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
    Serial.print("ADC Calibration: Using eFuse Vref: ");
    Serial.print(adc_chars.vref);
    Serial.println("mV");
    vref = adc_chars.vref;
  } else {
    Serial.print("ADC Calibration: Using default Vref: ");
    Serial.print(vref);
    Serial.println("mV (eFuse not available)");
  }
  Serial.println("================================");

  // Initialize e-paper display
  epd_init();
  drawInitialScreen();

  // Initialize STL collections for display data
  initCollections();

  // Connect to WiFi
  ensureWiFi();

  // Setup OTA (Over-The-Air) updates
  // IMPORTANT: If you change the password below, also update .platformio_env
  // file The .platformio_env file contains OTA_IP and OTA_PASSWORD for
  // PlatformIO uploads See extra_scripts/load_env.py and
  // .platformio_env.example for details
  ArduinoOTA.setHostname(
      "epd47-dashboard"); // Hostname for OTA (appears in network)
  ArduinoOTA.setPassword(
      OTA_PASSWORD); // OTA password - keep in secrets.h (matches .platformio_env)
  if (String(OTA_PASSWORD) == "CHANGE_ME_OTA_PASSWORD") {
    Serial.println("⚠ WARNING: OTA password is still the placeholder. Update OTA_PASSWORD_VALUE in secrets.h and OTA_PASSWORD in .platformio_env.");
  }

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
    // Turn off display during update
    epd_poweroff_all();
  });

  ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });

  ArduinoOTA.begin();
  Serial.println("OTA ready");
  enableOtaWindow(); // Allow OTA immediately after boot for convenience

  // Init NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Wait for NTP sync and initialize date
  delay(2000);  // Give NTP time to sync
  updateDate(); // Initialize date display and tracking

  // Initial Fetch and full dashboard draw
  fetchWeather(currentWeather);
  fetchTodos(todoList);
  fetchCalendar(calendarEvents);
  fetchQuotes(quotes, currentQuoteIndex);

  drawDashboard();

  // Initialize update timers after initial draw
  unsigned long now = millis();
  lastWeatherUpdate   = now;
  lastCalTodoUpdate   = now;
  lastQuoteFetch      = now;
  lastQuoteRotation   = now;
  lastBatteryUpdate   = now;

  // Read battery once at the end of setup so we have up-to-date info
  Serial.println("=== Battery-optimized mode: finalizing setup cycle ===");
  batteryInfo = readBattery();
  Serial.print("Battery: ");
  Serial.print(batteryInfo.voltage, 3);
  Serial.print("V (");
  Serial.print(batteryInfo.percentage);
  Serial.println("%)");

  if (!debugMode) {
    Serial.print("Battery mode active - entering deep sleep for ");
    Serial.print(SLEEP_INTERVAL_MINUTES);
    Serial.println(" minutes.");
    // Turn off WiFi before entering deep sleep
    forceWifiOff();
    // Configure wake-up timer: SLEEP_INTERVAL_MINUTES * 60 seconds * 1,000,000 microseconds
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_INTERVAL_MINUTES * 60ULL * 1000000ULL);
    Serial.println("Entering deep sleep now...");
    esp_deep_sleep_start();
  } else {
    Serial.println("Debug/OTA mode active - staying awake and running loop().");
  }
}

/**
 * Main loop - Handles periodic updates and OTA
 * Runs continuously, updating different sections at their configured intervals
 */
// NOTE: In normal battery-optimized mode, the device enters deep sleep at the end of setup()
// and loop() is never executed. loop() is only used when debugMode is true (BUTTON_1 held at boot)
// to allow continuous OTA and live debugging.
void loop() {
  if (!debugMode) {
    // In battery mode we should be in deep sleep; this is a safety guard.
    return;
  }
  // Handle OTA updates only when OTA window is active
  if (otaEnabled) {
    ArduinoOTA.handle();
    if (millis() > otaWindowEnds) {
      Serial.println("OTA window expired");
      otaEnabled = false;
    } else {
      // While OTA is active, skip the rest of the loop to avoid
      // network activity or e-paper operations interfering with the upload.
      return;
    }
  }

  // Simple button poll to re-enable OTA (active low, edge detected)
  int buttonState = digitalRead(BUTTON_1);
  if (buttonState == LOW && lastButtonState == HIGH) {
    Serial.println("OTA button pressed - enabling OTA window");
    enableOtaWindow();
    ensureWiFi();
  }
  lastButtonState = buttonState;

  // If OTA is enabled but WiFi dropped, reconnect
  if (otaEnabled && WiFi.status() != WL_CONNECTED) {
    ensureWiFi();
  }

  // If OTA is not active, ensure WiFi is off (LED off)
  if (!otaEnabled) {
    disableWiFiIfAllowed();
  }

  unsigned long now = millis();
  bool lowBatteryMode =
      (batteryInfo.percentage > 0 && batteryInfo.percentage < 20);
  unsigned long weatherInterval =
      WEATHER_UPDATE_INTERVAL_MS * (lowBatteryMode ? 2 : 1);
  unsigned long calTodoInterval =
      CAL_TODO_UPDATE_INTERVAL_MS * (lowBatteryMode ? 2 : 1);
  unsigned long quoteFetchInterval =
      QUOTE_FETCH_INTERVAL_MS * (lowBatteryMode ? 2 : 1);
  unsigned long quoteRotateInterval =
      QUOTE_ROTATION_INTERVAL_MS * (lowBatteryMode ? 2 : 1);

  // Weather update every hour
  if (lastWeatherUpdate == 0 ||
      (now - lastWeatherUpdate) > weatherInterval) {
    lastWeatherUpdate = now;

    Serial.println("Updating Weather...");
    if (fetchWeather(currentWeather)) {
      updateWeatherSection();
    }
  }

  // Calendar and Todo update every 6 hours
  if (lastCalTodoUpdate == 0 ||
      (now - lastCalTodoUpdate) > calTodoInterval) {
    lastCalTodoUpdate = now;

    Serial.println("Updating Calendar and Todo...");
    bool todoChanged = fetchTodos(todoList);
    bool calendarChanged = fetchCalendar(calendarEvents);
    if (todoChanged || calendarChanged) {
      updateCalendarTodoSections();
    }
  }

  // Clock removed to save refreshes

  // Quote fetch update (once per day)
  if (lastQuoteFetch == 0 || (now - lastQuoteFetch) > quoteFetchInterval) {
    lastQuoteFetch = now;
    Serial.println("Fetching new quotes...");
    if (fetchQuotes(quotes, currentQuoteIndex)) {
      redrawQuoteSection();
    }
  }

  // Quote rotation update (every 6 hours)
  if (lastQuoteRotation == 0 ||
      (now - lastQuoteRotation) > quoteRotateInterval) {
    lastQuoteRotation = now;
    Serial.println("Rotating quote...");
    rotateQuote(quotes, currentQuoteIndex,
                quoteArea); // rotateQuote() handles drawing and power management
  }

  // Midnight full refresh (check once per minute)
  if (lastMidnightCheck == 0 || (now - lastMidnightCheck) > MIDNIGHT_CHECK_INTERVAL_MS) {
    lastMidnightCheck = now;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      if (timeinfo.tm_hour == 0 && timeinfo.tm_min == 0 &&
          timeinfo.tm_mday != lastMidnightDay) {
        Serial.println("Midnight detected, scheduling full refresh");
        lastMidnightDay = timeinfo.tm_mday;
        fullRefreshScheduled = true;
      }
    }
  }

  // Run scheduled full refresh (e.g., at midnight)
  if (fullRefreshScheduled) {
    fullRefreshScheduled = false;
    drawDashboard();
  }

  // Battery display removed to avoid frequent refresh; still measure for logging
  if (lastBatteryUpdate == 0 ||
      (now - lastBatteryUpdate) > BATTERY_UPDATE_INTERVAL_MS) {
    lastBatteryUpdate = now;

    Serial.println("=== Battery Reading (no display update) ===");
    epd_poweron();
    delay(10);
    batteryInfo = readBattery();
    Serial.print("Battery: ");
    Serial.print(batteryInfo.voltage, 3);
    Serial.print("V (");
    Serial.print(batteryInfo.percentage);
    Serial.println("%)");
    epd_poweroff();
  }

  delay(1000);
}

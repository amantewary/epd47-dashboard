#include "calendar.h"

#include "config.h"
#include "ha_client.h"
#include "types.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <time.h>

extern JsonDocument haArrayDoc;
extern const std::vector<const char *> ENTITY_CALENDARS;

void disableWiFiIfAllowed();

// Helper to get URL-encoded ISO8601 string for Calendar API
static String getISOTime(time_t t) {
  struct tm *tm = localtime(&t);
  char buf[30];
  // Format: YYYY-MM-DDTHH:MM:SS
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tm);
  return String(buf);
}

static bool calendarsEqual(const std::vector<CalendarEvent> &a,
                           const std::vector<CalendarEvent> &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].title != b[i].title || a[i].startTime != b[i].startTime ||
        a[i].date != b[i].date || a[i].isoDateTime != b[i].isoDateTime) {
      return false;
    }
  }
  return true;
}

bool fetchCalendar(std::vector<CalendarEvent> &calendarEvents) {
  Serial.println("=== fetchCalendar() called ===");
  std::vector<CalendarEvent> newEvents;
  haArrayDoc.clear();

  // Get current time and end time (7 days later)
  time_t now;
  time(&now);
  time_t end = now + (7 * 24 * 60 * 60);

  String startStr = getISOTime(now);
  String endStr = getISOTime(end);

  Serial.print("Calendar time range: ");
  Serial.print(startStr);
  Serial.print(" to ");
  Serial.println(endStr);

  // URL Encode the timestamps (specifically :)
  startStr.replace(":", "%3A");
  endStr.replace(":", "%3A");

  Serial.print("Number of calendar entities configured: ");
  Serial.println(ENTITY_CALENDARS.size());

  int totalEvents = 0;
  for (const char *entity : ENTITY_CALENDARS) {
    Serial.println("");
    Serial.print(">>> Processing calendar entity: ");
    Serial.println(entity);
    // Use the Calendar API to get events in range
    // URL: /api/calendars/{entity}?start={start}&end={end}
    String url = buildHaUrl(String("/api/calendars/") + entity + "?start=" +
                            startStr + "&end=" + endStr);

    Serial.print("Fetching Calendar URL: ");
    Serial.println(url);
    haArrayDoc.clear();

    if (!fetchJson(url, haArrayDoc)) {
      Serial.print("ERROR: Failed to fetch calendar: ");
      Serial.println(entity);
      Serial.print("URL was: ");
      Serial.println(url);
      Serial.println(">>> Moving to next calendar entity");
      continue;
    }

    Serial.println(">>> fetchJson succeeded");

    // Check if we got valid calendar data
    if (!haArrayDoc.is<JsonArray>()) {
      Serial.print("WARNING: Calendar response was not an array for entity: ");
      Serial.println(entity);
      Serial.print("Response type: ");
      if (haArrayDoc.is<JsonObject>()) {
        Serial.println("Object (unexpected)");
        serializeJson(haArrayDoc, Serial);
        Serial.println();
      } else {
        Serial.println("Unknown");
      }
      continue;
    }

    Serial.print("Calendar JSON fetched successfully for: ");
    Serial.println(entity);

    // The API returns a JSON Array of events directly
    if (haArrayDoc.is<JsonArray>()) {
      JsonArray events = haArrayDoc.as<JsonArray>();
      Serial.print(">>> Found ");
      Serial.print(events.size());
      Serial.print(" events in ");
      Serial.println(entity);

      if (events.size() == 0) {
        Serial.println(">>> No events in this calendar (empty array)");
      }

      for (JsonVariant v : events) {
        CalendarEvent evt;

        // "start": { "dateTime": "..." } or { "date": "..." }
        if (v["start"]["dateTime"].is<String>()) {
          evt.isoDateTime = v["start"]["dateTime"].as<String>();
        } else if (v["start"]["date"].is<String>()) {
          evt.isoDateTime = v["start"]["date"].as<String>(); // All day
        }

        evt.title = v["summary"].as<String>();
        newEvents.push_back(evt);
        totalEvents++;

        Serial.print("  Event: ");
        Serial.print(evt.title);
        Serial.print(" at ");
        Serial.println(evt.isoDateTime);
      }
    } else {
      Serial.print("ERROR: Calendar response was not an array for ");
      Serial.println(entity);
      Serial.print("Response type: ");
      if (haArrayDoc.is<JsonObject>()) {
        Serial.println("Object");
        serializeJson(haArrayDoc, Serial);
        Serial.println();
      } else {
        Serial.println("Unknown");
      }
    }
  }

  Serial.print("Total calendar events fetched: ");
  Serial.println(totalEvents);

  // Format for display BEFORE sorting and limiting
  for (auto &evt : newEvents) {
    if (evt.isoDateTime.length() >= 10) {
      // Simple parsing assuming ISO format YYYY-MM-DD...
      evt.date = evt.isoDateTime.substring(5, 10); // MM-DD
      if (evt.isoDateTime.length() >= 16) {
        evt.startTime = evt.isoDateTime.substring(11, 16); // HH:MM
      } else {
        evt.startTime = "All Day";
      }
    }
  }

  // Sort by ISO datetime FIRST (to get chronological order)
  std::sort(newEvents.begin(), newEvents.end(),
            [](const CalendarEvent &a, const CalendarEvent &b) {
              return a.isoDateTime < b.isoDateTime;
            });

  // NOW limit to display area (increase to 10 items to show more events for the
  // week) Each event takes 2 lines (date/time + title), so 10 items = 5 events
  if (newEvents.size() > 10) {
    newEvents.resize(10);
  }

  Serial.print("Calendar events after sorting and limiting: ");
  Serial.println(newEvents.size());

  Serial.print("Final calendar events count after formatting: ");
  Serial.println(newEvents.size());
  Serial.println("=== fetchCalendar() complete ===");

  bool changed = !calendarsEqual(calendarEvents, newEvents);
  if (changed) {
    calendarEvents = newEvents;
  }
  disableWiFiIfAllowed();
  return changed;
}

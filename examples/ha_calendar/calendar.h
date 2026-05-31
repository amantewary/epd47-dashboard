#pragma once

#include "types.h"
#include <vector>

// Fetch upcoming calendar events from Home Assistant.
// Returns true when the visible display data changed.
bool fetchCalendar(std::vector<CalendarEvent> &calendarEvents);

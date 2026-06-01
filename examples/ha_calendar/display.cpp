#include "display.h"

#include "firasans.h"
#include "firasans_medium.h"
#include "firasans_small.h"
#include "todo_icons.h"
#include "weather_icons.h"
#include <algorithm>

// Draw bitmap icons using pre-defined data
void drawBitmapIcon(int32_t x, int32_t y, const uint8_t *icon_data,
                    uint32_t width, uint32_t height) {
  Rect_t icon_area = {
      .x = x, .y = y, .width = (int32_t)width, .height = (int32_t)height};
  epd_draw_image(icon_area, (uint8_t *)icon_data, BLACK_ON_WHITE);
}

// Draw a text-based divider line using dashes
void drawTextDivider(int32_t x, int32_t y, int32_t width) {
  // Create a string of dashes (approximately 1 dash per 10 pixels for
  // visibility)
  int32_t numDashes = width / 10;
  if (numDashes < 1)
    numDashes = 1;

  String dividerText = "";
  for (int32_t i = 0; i < numDashes; i++) {
    dividerText += "-";
  }

  // Draw the divider text
  int32_t cursor_x = x;
  int32_t cursor_y = y + FiraSans.advance_y + FiraSans.descender;
  writeln((GFXfont *)&FiraSans, dividerText.c_str(), &cursor_x, &cursor_y,
          NULL);
}

// Basic word-wrap: splits a string into lines that fit within maxChars.
// If a single word exceeds maxChars, it will be split mid-word.
std::vector<String> wrapText(const String &text, int maxChars) {
  std::vector<String> lines;
  if (maxChars <= 0) {
    lines.push_back(text);
    return lines;
  }

  int start = 0;
  while (start < text.length()) {
    int end = start + maxChars;
    if (end >= text.length()) {
      lines.push_back(text.substring(start));
      break;
    }

    int lastSpace = text.lastIndexOf(' ', end);
    if (lastSpace <= start) {
      lines.push_back(text.substring(start, end));
      start = end;
    } else {
      lines.push_back(text.substring(start, lastSpace));
      start = lastSpace + 1;
      // Skip any remaining consecutive spaces so next line doesn't start with spaces
      while (start < text.length() && text.charAt(start) == ' ') {
        start++;
      }
    }
  }

  return lines;
}

void drawList(const Rect_t &area, const std::vector<String> &lines,
              String header, bool drawIcons, int lineSpacing,
              const uint8_t *headerIconData, uint32_t headerIconWidth,
              uint32_t headerIconHeight) {
  epd_clear_area(area);

  int32_t cursor_x = area.x;
  int32_t cursor_y = area.y + FiraSans.advance_y + FiraSans.descender;

  // Draw Header (only if provided and non-empty)
  if (header.length() > 0) {
    // Draw header icon if provided
    if (headerIconData != NULL && headerIconWidth > 0 && headerIconHeight > 0) {
      int32_t icon_x = cursor_x;
      int32_t icon_y = cursor_y - headerIconHeight -
                       2; // Position icon slightly above text baseline
      drawBitmapIcon(icon_x, icon_y, headerIconData, headerIconWidth,
                     headerIconHeight);
      cursor_x += headerIconWidth + 5; // Space after icon
    }

    writeln((GFXfont *)&FiraSansMedium, header.c_str(), &cursor_x, &cursor_y,
            NULL);

    // If this is UPCOMING header, add countdown next to it
    if (header == "UPCOMING") {
      cursor_x += 10; // Space between header and countdown
      extern int getMinutesUntilNextEvent();
      int minutesUntilNext = getMinutesUntilNextEvent();
      if (minutesUntilNext >= 0) {
        int hours = minutesUntilNext / 60;
        int mins = minutesUntilNext % 60;
        char countdownStr[20];
        if (hours > 0) {
          snprintf(countdownStr, sizeof(countdownStr), "(Next: %dh %dm)", hours,
                   mins);
        } else {
          snprintf(countdownStr, sizeof(countdownStr), "(Next: %dm)", mins);
        }
        writeln((GFXfont *)&FiraSansSmall, countdownStr, &cursor_x, &cursor_y,
                NULL);
      }
    }

    cursor_y += 35; // Increased header spacing to separate header from items
  }

  // Draw Items with word wrapping consideration
  int itemIndex = 0;
  for (const String &line : lines) {
    cursor_x = area.x;

    // Draw icon if needed (for todos)
    if (drawIcons && itemIndex < (int)lines.size()) {
      bool isOverdue = (line.length() > 0 && line[0] == '!');
      int32_t icon_x = cursor_x;
      int32_t icon_y = cursor_y - icon_checkbox_height - 2;

      // Draw checkbox icon (overdue items use checked box)
      if (isOverdue) {
        drawBitmapIcon(icon_x, icon_y, icon_checkbox_checked_data,
                       icon_checkbox_checked_width,
                       icon_checkbox_checked_height);
      } else {
        drawBitmapIcon(icon_x, icon_y, icon_checkbox_data, icon_checkbox_width,
                       icon_checkbox_height);
      }

      cursor_x += icon_checkbox_width + 5; // Space after icon
    }

    const int32_t textStartX = cursor_x; // For wrapped lines after first

    // Truncate long lines to fit in area width
    String displayLine = line;

    // Store original line for detection (before any modifications)
    String originalLine = line;

    // Remove icon prefix if present
    if (displayLine.length() > 2 &&
        (displayLine[0] == 'X' || displayLine[0] == '>')) {
      displayLine = displayLine.substring(2);
    }
    // Adjust max length based on area width (450px = ~28 chars, 920px = ~50
    // chars)
    int maxChars = (area.width < 500)
                       ? 28
                       : 50; // Half width gets 28 chars, full width gets 50
    std::vector<String> wrappedLines = wrapText(displayLine, maxChars);

    // Check if this is a date/time line (starts with date pattern like "1/15"
    // or contains "All Day") Use ORIGINAL line (before truncation) for
    // detection
    bool isDateTimeLine = false;

    // More robust check: line must start with a digit and contain "/" within
    // first 6 chars OR contain "All Day") Also check: lines that start with
    // spaces are NOT date/time (they're indented titles)
    if (originalLine.indexOf("All Day") >= 0) {
      isDateTimeLine = true;
    } else if (originalLine.length() > 0) {
      // Check if first character is a digit (month) - NOT a space
      char firstChar = originalLine.charAt(0);
      if (firstChar >= '0' && firstChar <= '9') {
        // Now check if there's a "/" within first 6 characters (covers "1/15",
        // "10/15", "12/1")
        int slashPos = originalLine.indexOf('/');
        if (slashPos >= 1 && slashPos <= 5) {
          isDateTimeLine = true;
        }
      }
    }

    // Debug logging for UPCOMING events
    if (!drawIcons &&
        itemIndex < 6) { // Log first 6 items to see date/time pairs
      Serial.print("Line ");
      Serial.print(itemIndex);
      Serial.print(": original='");
      Serial.print(
          originalLine.substring(0, std::min(20, (int)originalLine.length())));
      Serial.print("' display='");
      Serial.print(
          displayLine.substring(0, std::min(20, (int)displayLine.length())));
      Serial.print("' isDateTime=");
      Serial.println(isDateTimeLine);
    }

    // Render wrapped lines
    bool firstWrapped = true;
    for (const auto &wrapped : wrappedLines) {
      cursor_x = firstWrapped ? cursor_x : textStartX;

      // Use smaller font for date/time lines in calendar (not for todo items)
      if (isDateTimeLine && !drawIcons) {
        writeln((GFXfont *)&FiraSansSmall, wrapped.c_str(), &cursor_x,
                &cursor_y, NULL);
        cursor_y += lineSpacing + 2; // Extra spacing after small date/time line
      } else {
        writeln((GFXfont *)&FiraSansMedium, wrapped.c_str(), &cursor_x,
                &cursor_y, NULL);
        cursor_y += lineSpacing;
      }

      firstWrapped = false;
    }

    // Check if next line would overflow (check BEFORE incrementing for next
    // line)
    int nextY = cursor_y + lineSpacing;
    int maxY = area.y + area.height;
    Serial.print("drawList: After line ");
    Serial.print(itemIndex);
    Serial.print(", cursor_y=");
    Serial.print(cursor_y);
    Serial.print(", nextY would be=");
    Serial.print(nextY);
    Serial.print(", maxY=");
    Serial.print(maxY);
    Serial.print(", remaining=");
    Serial.print(maxY - cursor_y);
    Serial.println("px");

    if (nextY > maxY) {
      Serial.print(">>> STOPPING at item ");
      Serial.print(itemIndex);
      Serial.print(" - nextY (");
      Serial.print(nextY);
      Serial.print(") > maxY (");
      Serial.println(")");
      break;
    }
    itemIndex++;
  }

  Serial.print("drawList: Completed - drew ");
  Serial.print(itemIndex);
  Serial.print(" out of ");
  Serial.print(lines.size());
  Serial.println(" lines");
}

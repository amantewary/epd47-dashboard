#include "quotes.h"

#include "config.h"
#include "display.h"
#include "firasans_small.h"
#include "ha_client.h"
#include "types.h"
#include <ArduinoJson.h>
#include <algorithm>

// External state and helpers provided by the main sketch
extern JsonDocument haArrayDoc;
void disableWiFiIfAllowed();
std::vector<String> wrapText(const String &text, int maxChars);
void epd_poweron();
void epd_poweroff();

static bool quotesEqual(const std::vector<QuoteData> &a,
                        const std::vector<QuoteData> &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].author != b[i].author || a[i].text != b[i].text) {
      return false;
    }
  }
  return true;
}

bool fetchQuotes(std::vector<QuoteData> &quotes, int &currentQuoteIndex) {
  Serial.println("=== fetchQuotes() called ===");
  String url = buildHaUrl(String("/api/states/") + String(ENTITY_QUOTE));
  Serial.print("Quote URL: ");
  Serial.println(url);

  haArrayDoc.clear();

  if (!fetchJson(url, haArrayDoc)) {
    Serial.println("ERROR: Quote fetch failed - fetchJson returned false");
    return false;
  }

  Serial.println("Quote JSON fetched successfully");

  // Parse the quotes/entries array from attributes (accept both keys)
  JsonArray entries = haArrayDoc["attributes"]["quotes"].as<JsonArray>();
  if (entries.isNull()) {
    entries = haArrayDoc["attributes"]["entries"].as<JsonArray>();
  }
  if (entries.isNull()) {
    Serial.println("ERROR: No 'quotes' or 'entries' array found in attributes");
    return false;
  }

  Serial.print("Found ");
  Serial.print(entries.size());
  Serial.println(" quotes");

  std::vector<QuoteData> newQuotes;
  newQuotes.reserve(entries.size());

  // Parse each entry
  for (JsonObject entry : entries) {
    QuoteData quote;

    // Get author from title
    const char *author = entry["title"];
    if (author) {
      quote.author = String(author);
    } else {
      quote.author = "Unknown";
    }

    // Get quote text from summary (remove surrounding quotes if present)
    const char *summary = entry["summary"];
    if (summary) {
      String text = String(summary);
      // Remove leading/trailing quotes and backslashes
      text.trim();
      if (text.startsWith("\"") && text.endsWith("\"")) {
        text = text.substring(1, text.length() - 1);
      }
      // Remove escaped quotes
      text.replace("\\\"", "\"");
      quote.text = text;
    } else {
      quote.text = "";
    }

    // Only add if we have valid text
    if (quote.text.length() > 0) {
      newQuotes.push_back(quote);
      Serial.print("Added quote from ");
      Serial.print(quote.author);
      Serial.print(": ");
      Serial.println(quote.text.substring(0, 50)); // Print first 50 chars
    }
  }

  Serial.print("Total quotes stored: ");
  Serial.println(newQuotes.size());

  bool changed = !quotesEqual(quotes, newQuotes);
  if (changed) {
    quotes = newQuotes;
    currentQuoteIndex = 0;
  }

  Serial.println("=== fetchQuotes() complete ===");
  disableWiFiIfAllowed();
  return changed;
}

void drawQuote(const std::vector<QuoteData> &quotes, int currentQuoteIndex,
               const Rect_t &quoteArea) {
  if (quotes.empty()) {
    Serial.println("No quotes available");
    return;
  }

  // Get current quote
  QuoteData currentQuote = quotes[currentQuoteIndex];

  // Format quote text with quotes and author
  String quoteText = "\"" + currentQuote.text + "\" - " + currentQuote.author;

  Serial.print("Drawing quote: ");
  Serial.println(quoteText);

  // Estimate wrapping based on available width and font size
  const int estimatedCharWidth = 14; // Approx width for FiraSansSmall
  const int lineSpacing = 4;
  const int lineHeight = FiraSansSmall.advance_y + lineSpacing;
  const int maxCharsPerLine =
      std::max(20, quoteArea.width / estimatedCharWidth); // keep inside 920px
  const int maxQuoteLines = std::max(1, quoteArea.height / lineHeight);

  std::vector<String> wrappedLines = wrapText(quoteText, maxCharsPerLine);

  bool truncated = false;
  if ((int)wrappedLines.size() > maxQuoteLines) {
    truncated = true;
    wrappedLines.resize(maxQuoteLines);
  }

  // If we truncated, ellipsize the last line to signal more text
  if (truncated && !wrappedLines.empty()) {
    String &lastLine = wrappedLines.back();
    if (lastLine.length() > (maxCharsPerLine - 3)) {
      lastLine = lastLine.substring(0, maxCharsPerLine - 3);
    }
    if (!lastLine.endsWith("...")) {
      lastLine += "...";
    }
  }

  // Clear and draw wrapped quote lines
  epd_clear_area(quoteArea);
  int32_t cursor_x = quoteArea.x;
  int32_t cursor_y =
      quoteArea.y + FiraSansSmall.advance_y + FiraSansSmall.descender;

  for (const auto &line : wrappedLines) {
    cursor_x = quoteArea.x;
    writeln((GFXfont *)&FiraSansSmall, line.c_str(), &cursor_x, &cursor_y,
            NULL);
    cursor_y += lineHeight;
  }
}

void rotateQuote(std::vector<QuoteData> &quotes, int &currentQuoteIndex,
                 const Rect_t &quoteArea) {
  if (quotes.empty()) {
    return;
  }

  currentQuoteIndex++;
  if (currentQuoteIndex >= (int)quotes.size()) {
    currentQuoteIndex = 0;
  }

  Serial.print("Rotating to quote index: ");
  Serial.println(currentQuoteIndex);

  // Redraw the quote section
  epd_poweron();
  drawQuote(quotes, currentQuoteIndex, quoteArea);
  epd_poweroff();
}

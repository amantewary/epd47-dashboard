#include "mqtt_client.h"

#include "config.h"
#include "secrets.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

// ---------- retained-message capture ----------

struct SubEntry {
  String topic;
  MqttPayload *payload;
};

static WiFiClient mqttWifiClient;
static PubSubClient mqttClient(mqttWifiClient);
static SubEntry *gSubs = nullptr;
static int gSubCount = 0;
static int gReceivedCount = 0;

// Persistent mode globals
static bool gPersistent = false;
static String gPersistentPrefix;
static MqttPayload gPersistentWeather;
static MqttPayload gPersistentTodos;
static MqttPayload gPersistentCalendar;
static MqttPayload gPersistentQuotes;
static unsigned long gPersistentLastConnAttempt = 0;

static void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String topicStr = String(topic);
  String payloadStr;
  payloadStr.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) {
    payloadStr += (char)payload[i];
  }

  // Persistent mode – store latest, mark as available
  if (gPersistent) {
    bool matched = true;
    if (topicStr == gPersistentPrefix + "/weather") {
      gPersistentWeather.payload = payloadStr;
      gPersistentWeather.received = true;
    } else if (topicStr == gPersistentPrefix + "/todos") {
      gPersistentTodos.payload = payloadStr;
      gPersistentTodos.received = true;
    } else if (topicStr == gPersistentPrefix + "/calendar") {
      gPersistentCalendar.payload = payloadStr;
      gPersistentCalendar.received = true;
    } else if (topicStr == gPersistentPrefix + "/quotes") {
      gPersistentQuotes.payload = payloadStr;
      gPersistentQuotes.received = true;
    } else {
      matched = false;
    }
    if (matched) {
      Serial.printf("MQTT PUSH: <%s> (%u bytes)\n", topic, length);
    }
    return;
  }

  // One-shot mode – first retained message per topic wins
  for (int i = 0; i < gSubCount; ++i) {
    if (topicStr == gSubs[i].topic && gSubs[i].payload != nullptr &&
        !gSubs[i].payload->received) {
      gSubs[i].payload->topic = topicStr;
      gSubs[i].payload->payload = payloadStr;
      gSubs[i].payload->received = true;
      ++gReceivedCount;
      Serial.printf("MQTT: received <%s> (%u bytes)\n", topic, length);
      return;
    }
  }
  Serial.printf("MQTT: ignored <%s> (unexpected topic)\n", topic);
}

// ---------- public API ----------

bool mqttFetchAll(const String &prefix, unsigned long timeoutMs,
                  MqttPayload &weather, MqttPayload &todos,
                  MqttPayload &calendar, MqttPayload &quotes) {
  // Build topic list
  SubEntry subs[4] = {
    {prefix + "/weather",  &weather},
    {prefix + "/todos",    &todos},
    {prefix + "/calendar", &calendar},
    {prefix + "/quotes",   &quotes},
  };
  gSubs = subs;
  gSubCount = 4;
  gReceivedCount = 0;

  String broker = MQTT_BROKER_HOST;
  uint16_t port = MQTT_BROKER_PORT;

  mqttClient.setServer(broker.c_str(), port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setSocketTimeout(3); // shorter timeout per socket op

  // Connect – retry for up to 5 s
  Serial.printf("MQTT: connecting to %s:%u ...\n", broker.c_str(), port);
  unsigned long start = millis();
  while (!mqttClient.connected() && millis() - start < 5000) {
    const char *user = strlen(MQTT_USERNAME) > 0 ? MQTT_USERNAME : nullptr;
    const char *pass = strlen(MQTT_PASSWORD) > 0 ? MQTT_PASSWORD : nullptr;
    if (mqttClient.connect("epd47-dashboard", user, pass)) {
      Serial.println("MQTT: connected");
      break;
    }
    delay(100);
  }

  if (!mqttClient.connected()) {
    Serial.println("MQTT: connection FAILED");
    gSubs = nullptr;
    return false;
  }

  // Subscribe to all retained topics
  Serial.println("MQTT: subscribing ...");
  for (int i = 0; i < gSubCount; ++i) {
    bool ok = mqttClient.subscribe(subs[i].topic.c_str());
    Serial.printf("  %s -> %s\n", subs[i].topic.c_str(), ok ? "OK" : "FAIL");
  }

  // Flush retained messages – loop until all 4 received or timeout
  start = millis();
  while (millis() - start < timeoutMs && gReceivedCount < gSubCount) {
    mqttClient.loop();
    delay(10);
  }

  mqttClient.disconnect();
  Serial.printf("MQTT: done – received %d/%d topics\n", gReceivedCount, gSubCount);

  gSubs = nullptr;
  return gReceivedCount > 0;
}

// ---------- MQTT payload parsers ----------

bool parseMqttWeather(const String &json, WeatherData &weather) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("MQTT weather parse error: %s\n", err.c_str());
    return false;
  }

  const char *state = doc["state"];
  if (!state) return false;

  float temp = doc["temperature"] | NAN;
  if (isnan(temp)) return false;

  float displayTemp = temp;
  const char *unit = " C";
  if (USE_FAHRENHEIT) {
    displayTemp = temp * 9.0f / 5.0f + 32.0f;
    unit = " F";
  }

  WeatherData w;
  w.condition = String(state);
  w.temperature = String(displayTemp, 1) + unit;

  bool changed = (w.condition != weather.condition ||
                  w.temperature != weather.temperature);
  if (changed) weather = w;
  return changed;
}

static String getTodayDateString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "";
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

bool parseMqttTodos(const String &json, std::vector<TodoItem> &todoList) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("MQTT todos parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray items = doc.as<JsonArray>();
  if (items.isNull()) return false;

  String today = getTodayDateString();
  if (today.length() == 0) return false;

  std::vector<TodoItem> overdue;
  std::vector<TodoItem> dueToday;

  for (JsonVariant v : items) {
    String summary = v["summary"].as<String>();
    String status = v["status"].as<String>();
    if (status == "completed" || summary.length() == 0) continue;

    String due = v["due"].as<String>();
    if (due.length() < 10) continue;
    String dueDate = due.substring(0, 10);

    int cmp = dueDate.compareTo(today);
    if (cmp > 0) continue; // skip future

    TodoItem item;
    item.text = summary;
    item.dueDate = dueDate;
    item.overdue = (cmp < 0);

    if (item.overdue) overdue.push_back(item);
    else dueToday.push_back(item);
  }

  std::vector<TodoItem> merged;
  merged.reserve(overdue.size() + dueToday.size());
  merged.insert(merged.end(), overdue.begin(), overdue.end());
  merged.insert(merged.end(), dueToday.begin(), dueToday.end());

  const size_t MAX_TODOS = 8;
  if (merged.size() > MAX_TODOS) merged.resize(MAX_TODOS);

  // compare
  bool changed = merged.size() != todoList.size();
  if (!changed) {
    for (size_t i = 0; i < merged.size(); ++i) {
      if (merged[i].text != todoList[i].text ||
          merged[i].dueDate != todoList[i].dueDate ||
          merged[i].overdue != todoList[i].overdue) {
        changed = true;
        break;
      }
    }
  }

  if (changed) todoList = merged;
  return changed;
}

bool parseMqttCalendar(const String &json, std::vector<CalendarEvent> &events) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("MQTT calendar parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return false;

  // Get time bounds (7 days from now)
  time_t now;
  time(&now);
  time_t end = now + 7 * 24 * 3600;

  std::vector<CalendarEvent> newEvents;

  for (JsonVariant v : arr) {
    String iso = v["start"].as<String>();
    if (iso.length() == 0) continue;

    // Try dateTime first (full ISO), fall back to date (all-day)
    String title = v["summary"].as<String>();
    CalendarEvent evt;
    evt.title = title;
    evt.isoDateTime = iso;

    // Parse date parts from ISO string
    if (iso.length() >= 10) {
      evt.date = iso.substring(5, 10); // MM-DD
      if (iso.length() >= 16) evt.startTime = iso.substring(11, 16); // HH:MM
      else evt.startTime = "All Day";
    }

    // Optionally filter by time range here if desired
    newEvents.push_back(evt);
  }

  // Sort chronologically
  std::sort(newEvents.begin(), newEvents.end(),
            [](const CalendarEvent &a, const CalendarEvent &b) {
              return a.isoDateTime < b.isoDateTime;
            });

  if (newEvents.size() > 10) newEvents.resize(10);

  bool changed = newEvents.size() != events.size();
  if (!changed) {
    for (size_t i = 0; i < newEvents.size(); ++i) {
      if (newEvents[i].title != events[i].title ||
          newEvents[i].startTime != events[i].startTime ||
          newEvents[i].date != events[i].date ||
          newEvents[i].isoDateTime != events[i].isoDateTime) {
        changed = true;
        break;
      }
    }
  }

  if (changed) events = newEvents;
  return changed;
}

bool parseMqttQuotes(const String &json, std::vector<QuoteData> &quotes,
                     int &currentQuoteIndex) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("MQTT quotes parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return false;

  std::vector<QuoteData> newQuotes;
  newQuotes.reserve(arr.size());

  for (JsonObject entry : arr) {
    QuoteData q;
    const char *author = entry["title"];
    q.author = author ? String(author) : "Unknown";

    const char *text = entry["summary"];
    if (text) {
      String t = String(text);
      t.trim();
      if (t.startsWith("\"") && t.endsWith("\""))
        t = t.substring(1, t.length() - 1);
      t.replace("\\\"", "\"");
      q.text = t;
    } else {
      q.text = "";
    }

    if (q.text.length() > 0) newQuotes.push_back(q);
  }

  bool changed = newQuotes.size() != quotes.size();
  if (!changed) {
    for (size_t i = 0; i < newQuotes.size(); ++i) {
      if (newQuotes[i].author != quotes[i].author ||
          newQuotes[i].text != quotes[i].text) {
        changed = true;
        break;
      }
    }
  }

  if (changed) {
    quotes = newQuotes;
    currentQuoteIndex = 0;
  }
  return changed;
}

// ---------- persistent (push) mode ----------

static bool mqttConnectPersistent() {
  if (mqttClient.connected()) return true;

  String broker = MQTT_BROKER_HOST;
  uint16_t port = MQTT_BROKER_PORT;
  mqttClient.setServer(broker.c_str(), port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setSocketTimeout(3);

  const char *user = strlen(MQTT_USERNAME) > 0 ? MQTT_USERNAME : nullptr;
  const char *pass = strlen(MQTT_PASSWORD) > 0 ? MQTT_PASSWORD : nullptr;
  bool ok = mqttClient.connect("epd47-dashboard", user, pass);

  if (ok) {
    Serial.println("MQTT persistent: connected");
    // Re-subscribe
    mqttClient.subscribe((gPersistentPrefix + "/weather").c_str());
    mqttClient.subscribe((gPersistentPrefix + "/todos").c_str());
    mqttClient.subscribe((gPersistentPrefix + "/calendar").c_str());
    mqttClient.subscribe((gPersistentPrefix + "/quotes").c_str());
  }
  return ok;
}

bool mqttPersistentBegin(const String &prefix) {
  gPersistent = true;
  gPersistentPrefix = prefix;
  mqttConnectPersistent();
  return mqttClient.connected();
}

void mqttPersistentLoop() {
  if (!gPersistent) return;

  // Reconnect if dropped (throttled to once per 10 s)
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - gPersistentLastConnAttempt > 10000) {
      gPersistentLastConnAttempt = now;
      mqttConnectPersistent();
    }
    return;
  }
  mqttClient.loop();
}

bool mqttPersistentGetWeather(WeatherData &w) {
  if (!gPersistentWeather.received) return false;
  gPersistentWeather.received = false;
  return parseMqttWeather(gPersistentWeather.payload, w);
}

bool mqttPersistentGetTodos(std::vector<TodoItem> &t) {
  if (!gPersistentTodos.received) return false;
  gPersistentTodos.received = false;
  return parseMqttTodos(gPersistentTodos.payload, t);
}

bool mqttPersistentGetCalendar(std::vector<CalendarEvent> &e) {
  if (!gPersistentCalendar.received) return false;
  gPersistentCalendar.received = false;
  return parseMqttCalendar(gPersistentCalendar.payload, e);
}

bool mqttPersistentGetQuotes(std::vector<QuoteData> &q, int &currentQuoteIndex) {
  if (!gPersistentQuotes.received) return false;
  gPersistentQuotes.received = false;
  return parseMqttQuotes(gPersistentQuotes.payload, q, currentQuoteIndex);
}

void mqttPersistentEnd() {
  mqttClient.disconnect();
  gPersistent = false;
}

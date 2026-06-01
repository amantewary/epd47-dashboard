#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "types.h"

struct MqttPayload {
  String topic;
  String payload;
  bool received = false;
};

bool mqttFetchAll(const String &prefix, unsigned long timeoutMs,
                  MqttPayload &weather, MqttPayload &todos,
                  MqttPayload &calendar, MqttPayload &quotes);

bool parseMqttWeather(const String &json, WeatherData &weather);
bool parseMqttTodos(const String &json, std::vector<TodoItem> &todoList);
bool parseMqttCalendar(const String &json, std::vector<CalendarEvent> &events);
bool parseMqttQuotes(const String &json, std::vector<QuoteData> &quotes, int &currentQuoteIndex);

// Persistent MQTT mode (used in debug/OTA mode for push updates)
bool mqttPersistentBegin(const String &prefix);
void mqttPersistentLoop();
bool mqttPersistentGetWeather(WeatherData &w);
bool mqttPersistentGetTodos(std::vector<TodoItem> &t);
bool mqttPersistentGetCalendar(std::vector<CalendarEvent> &e);
bool mqttPersistentGetQuotes(std::vector<QuoteData> &q, int &currentQuoteIndex);
void mqttPersistentEnd();

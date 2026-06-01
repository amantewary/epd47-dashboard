#include "todos.h"

#include "config.h"
#include "ha_client.h"
#include "types.h"
#include <ArduinoJson.h>
#include <time.h>

extern const std::vector<const char *> ENTITY_TODOS;

void disableWiFiIfAllowed();

// Helper to get today's date string (YYYY-MM-DD) from NTP
static String getTodayDateString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "";
  }
  char timeStringBuff[20];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d", &timeinfo);
  return String(timeStringBuff);
}

static bool todosEqual(const std::vector<TodoItem> &a,
                       const std::vector<TodoItem> &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].text != b[i].text || a[i].dueDate != b[i].dueDate ||
        a[i].overdue != b[i].overdue) {
      return false;
    }
  }
  return true;
}

bool fetchTodos(std::vector<TodoItem> &todoList) {
  // 1. Get current date for filtering
  String today = getTodayDateString();
  if (today.length() == 0) {
    return false;
  }

  std::vector<TodoItem> overdueTodos;
  std::vector<TodoItem> todayTodos;

  for (const char *entity : ENTITY_TODOS) {
    String url = buildHaUrl(
        "/api/services/todo/get_items?return_response=true");
    // Add status: needs_action to be explicit and match common usage
    String payload = String("{\"entity_id\": \"") + entity +
                     "\", \"status\": \"needs_action\"}";

    JsonDocument doc;
    if (!fetchJsonPost(url, payload, doc))
      continue;

    JsonArray items = doc["service_response"][entity]["items"];
    if (items.isNull()) {
      continue;
    }

    for (JsonVariant v : items) {
      String summary = v["summary"].as<String>();
      String status = v["status"].as<String>();

      // Skip completed items
      if (status == "completed") {
        continue;
      }

      // Only show items with a due date; skip items without due dates
      if (v["due"].isNull()) {
        continue;
      }

      String due = v["due"].as<String>();
      if (due.length() < 10) {
        continue;
      }

      // Compare due date to today (YYYY-MM-DD) to categorize
      String dueDate = due.substring(0, 10);
      int cmp = dueDate.compareTo(today);
      if (cmp > 0) {
        // Future items are not shown in this view
        continue;
      }

      TodoItem item;
      item.text = summary;
      item.dueDate = dueDate;
      item.overdue = (cmp < 0);

      if (item.overdue) {
        overdueTodos.push_back(item);
      } else {
        todayTodos.push_back(item);
      }
    }
  }

  // Combine overdue first, then today
  std::vector<TodoItem> newTodos;
  newTodos.reserve(overdueTodos.size() + todayTodos.size());
  newTodos.insert(newTodos.end(), overdueTodos.begin(), overdueTodos.end());
  newTodos.insert(newTodos.end(), todayTodos.begin(), todayTodos.end());

  // Limit to what fits comfortably on screen
  const size_t MAX_TODOS = 8;
  if (newTodos.size() > MAX_TODOS) {
    newTodos.resize(MAX_TODOS);
  }

  bool changed = !todosEqual(todoList, newTodos);
  if (changed) {
    todoList = newTodos;
  }
  disableWiFiIfAllowed();
  return changed;
}

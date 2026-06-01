#pragma once

// Home Assistant Entity Configuration
// Copy this file to config.h and update with your own entity IDs
// config.h is NOT tracked by git

// Weather entity
#define ENTITY_WEATHER "weather.your_weather_entity"

// Quote sensor (optional)
#define ENTITY_QUOTE "sensor.quote_of_the_day"

// Todo entities - add your todo entity IDs here
#define ENTITY_TODOS_COUNT 2
#define ENTITY_TODO_1 "todo.your_todo_1"
#define ENTITY_TODO_2 "todo.your_todo_2"

// Calendar entities - add your calendar entity IDs here
#define ENTITY_CALENDARS_COUNT 2
#define ENTITY_CALENDAR_1 "calendar.your_calendar_1"
#define ENTITY_CALENDAR_2 "calendar.your_calendar_2"

// NTP Timezone Configuration
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC -18000   // UTC-5 (EST). Adjust for your timezone (UTC offset in seconds)
#define DAYLIGHT_OFFSET_SEC 3600 // 1 hour for DST

// Home Assistant connection
// Set to 1 to use HTTPS (recommended if your HA is served over SSL)
#define HA_USE_HTTPS 0

// Display preferences
#define USE_FAHRENHEIT 0 // Set to 1 to convert weather temperature to °F
#define USE_24H_TIME 0   // Set to 1 for 24-hour clock

// Deep sleep interval (minutes) when running in battery-optimized mode
#define SLEEP_INTERVAL_MINUTES 15

// MQTT mode – set to 0 to use the original HA REST API polling,
// or 1 to receive entity state via MQTT retained messages.
#define USE_MQTT          0

// MQTT topic prefix – the device subscribes to {prefix}/weather,
// {prefix}/todos, {prefix}/calendar, and {prefix}/quotes.
#define MQTT_TOPIC_PREFIX "epd47"

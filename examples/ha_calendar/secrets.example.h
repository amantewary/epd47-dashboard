#pragma once

// Fill this in and rename to secrets.h (not tracked by git)
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

#define HA_HOST_ADDR   "HOME_ASSISTANT_IP"
#define HA_PORT_NUM    8123

#define HA_TOKEN_VALUE "YOUR_LONG_LIVED_TOKEN"

// OTA password (must match OTA_PASSWORD in .platformio_env for uploads)
#define OTA_PASSWORD_VALUE "CHANGE_ME_OTA_PASSWORD"

// MQTT Broker (for MQTT data source mode)
#define MQTT_BROKER_HOST   HA_HOST_ADDR  // Usually the same as your HA server
#define MQTT_BROKER_PORT   1883
#define MQTT_USERNAME      ""            // Leave empty if no auth
#define MQTT_PASSWORD      ""

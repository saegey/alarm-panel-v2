#pragma once

#include <Arduino.h>

namespace Secrets {
constexpr char WIFI_SSID[] = "your-ssid";
constexpr char WIFI_PASSWORD[] = "your-password";
constexpr char MQTT_HOST[] = "192.168.1.50";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_CLIENT_ID[] = "home-keypad";
constexpr char MQTT_USERNAME[] = "mqtt-user";
constexpr char MQTT_PASSWORD[] = "mqtt-password";
constexpr char OTA_HOSTNAME[] = "alarm-panel";
constexpr char OTA_PASSWORD[] = "change-me";

constexpr char MQTT_TOPIC_GARAGE[] = "home/keypad/garage/toggle";
constexpr char MQTT_TOPIC_STATUS[] = "home/keypad/status";
constexpr char MQTT_TOPIC_DISPLAY_SET[] = "home/keypad/display/set";
constexpr char MQTT_TOPIC_GARAGE_DOOR_COMMAND[] = "garage/door/command";
constexpr char MQTT_TOPIC_GARAGE_DOOR_STATE[] = "garage/door/state";
constexpr char MQTT_TOPIC_WEATHER_STATE[] = "weather/kbfi/state";

constexpr char ALARMO_COMMAND_TOPIC[] = "alarmo/command";
constexpr char ALARMO_PERIMETER_STATE_TOPIC[] = "alarmo/perimeter/state";
constexpr char ALARMO_INTERIOR_STATE_TOPIC[] = "alarmo/interior/state";
constexpr char ALARMO_GARAGE_STATE_TOPIC[] = "alarmo/garage/state";

constexpr char MQTT_TOPIC_DISPLAY_TIMEOUT[] = "home/keypad/display/timeout";
}  // namespace Secrets

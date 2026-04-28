#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <ctype.h>

#include "app_state.h"
#include "secrets.h"

// --- Globals owned by this module ---

char masterAlarmState[32] = "unknown";
char garageAlarmState[32] = "unknown";
char garageDoorState[32] = "unknown";
char weatherText[32] = "--";

// --- Functions ---

bool mqttReady() {
  return WiFi.status() == WL_CONNECTED && mqttClient.connected();
}

void updateMqttStatusLabel(bool connected) {
  setStatusLabelText(connected ? "connected" : "disconnected",
                     connected ? lv_color_hex(0x81B29A) : lv_color_hex(0xE07A5F));
}

namespace {
void parseWeatherMessage(const char *message) {
  const char *temperatureKey = strstr(message, "\"temperature\":");
  const char *stateKey = strstr(message, "\"state\":\"");
  if (temperatureKey == nullptr || stateKey == nullptr) {
    return;
  }

  float temperature = 0.0f;
  if (sscanf(temperatureKey, "\"temperature\":%f", &temperature) != 1) {
    return;
  }

  stateKey += strlen("\"state\":\"");
  char condition[16] = "";
  size_t index = 0;
  while (stateKey[index] != '\0' && stateKey[index] != '"' && index < sizeof(condition) - 1) {
    condition[index] = stateKey[index];
    ++index;
  }
  condition[index] = '\0';

  if (condition[0] == '\0') {
    return;
  }

  condition[0] = static_cast<char>(toupper(static_cast<unsigned char>(condition[0])));
  snprintf(weatherText, sizeof(weatherText), "%d° %s", static_cast<int>(temperature + 0.5f),
           condition);
  updateWeatherDisplay();
}

bool publishAlarmoCommand(const char *command, const char *area, const char *code) {
  if (!mqttReady()) {
    Serial.println("[MQTT] Alarmo publish skipped, client not connected");
    return false;
  }

  char payload[160];
  const int written = (code != nullptr && code[0] != '\0')
                          ? snprintf(payload, sizeof(payload),
                                     "{\"command\":\"%s\",\"area\":\"%s\",\"code\":\"%s\"}",
                                     command, area, code)
                          : snprintf(payload, sizeof(payload),
                                     "{\"command\":\"%s\",\"area\":\"%s\"}", command, area);

  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    Serial.println("[MQTT] Alarmo payload too large");
    return false;
  }

  Serial.printf("[MQTT] Publish %s -> %s\n", Secrets::ALARMO_COMMAND_TOPIC, payload);
  return mqttClient.publish(Secrets::ALARMO_COMMAND_TOPIC, payload);
}

bool publishAlarmoCommand(const char *command, const char *code) {
  if (!mqttReady()) {
    Serial.println("[MQTT] Alarmo publish skipped, client not connected");
    return false;
  }

  char payload[128];
  const int written = (code != nullptr && code[0] != '\0')
                          ? snprintf(payload, sizeof(payload),
                                     "{\"command\":\"%s\",\"code\":\"%s\"}", command, code)
                          : snprintf(payload, sizeof(payload), "{\"command\":\"%s\"}", command);

  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    Serial.println("[MQTT] Alarmo payload too large");
    return false;
  }

  Serial.printf("[MQTT] Publish %s -> %s\n", Secrets::ALARMO_COMMAND_TOPIC, payload);
  return mqttClient.publish(Secrets::ALARMO_COMMAND_TOPIC, payload);
}
}  // namespace

bool executeAlarmAction(AlarmTarget target, AlarmAction action, const char *code) {
  if (target == AlarmTarget::GarageDoor) {
    const bool wantsOpen = action == AlarmAction::Arm;
    if ((wantsOpen && strcmp(garageDoorState, "open") == 0) ||
        (!wantsOpen && strcmp(garageDoorState, "closed") == 0)) {
      Serial.println("[MQTT] Garage door already in requested state");
      return true;
    }

    if (!mqttReady()) {
      Serial.println("[MQTT] Garage door publish skipped, client not connected");
      showToast("Not connected");
      return false;
    }

    Serial.printf("[MQTT] Publish %s -> TOGGLE\n", Secrets::MQTT_TOPIC_GARAGE_DOOR_COMMAND);
    mqttClient.publish(Secrets::MQTT_TOPIC_GARAGE_DOOR_COMMAND, "TOGGLE");
    showToast("Command sent");
    return true;
  }

  const char *command = nullptr;
  if (action == AlarmAction::Arm) {
    command = armModeCommand(selectedArmMode);
  } else if (action == AlarmAction::Disarm) {
    command = "disarm";
  }

  if (command == nullptr) {
    return false;
  }

  bool success = false;
  if (target == AlarmTarget::Master) {
    success = publishAlarmoCommand(command, code);
  } else {
    success = publishAlarmoCommand(command, "garage", code);
  }

  if (success) {
    showToast("Command sent");
  } else {
    showToast("Not connected");
  }
  return success;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  char message[96];
  const size_t copyLength = min(length, static_cast<unsigned int>(sizeof(message) - 1));
  memcpy(message, payload, copyLength);
  message[copyLength] = '\0';

  Serial.printf("[MQTT] Received %s -> %s\n", topic, message);

  if (strcmp(topic, Secrets::MQTT_TOPIC_DISPLAY_SET) == 0) {
    if (strcasecmp(message, "ON") == 0 || strcasecmp(message, "WAKE") == 0) {
      wakeDisplay();
      setDisplayBrightness(BRIGHTNESS_FULL);
      displayState = DisplayState::Active;
      return;
    }
    if (strcasecmp(message, "OFF") == 0 || strcasecmp(message, "SLEEP") == 0) {
      setDisplayBrightness(BRIGHTNESS_OFF);
      return;
    }
    if (strcasecmp(message, "TOGGLE") == 0) {
      if (displayState == DisplayState::Sleeping) {
        wakeDisplay();
      } else {
        setDisplayBrightness(BRIGHTNESS_OFF);
      }
      return;
    }
  }

  if (strcmp(topic, Secrets::MQTT_TOPIC_DISPLAY_TIMEOUT) == 0) {
    displayTimeoutSec = strtoul(message, nullptr, 10);
    Serial.printf("[UI] Display timeout set to %lu seconds\n",
                  static_cast<unsigned long>(displayTimeoutSec));
    saveSettings();
    if (displayTimeoutSec > 0) {
      resetIdleTimer();
    }
    return;
  }

  if (strcmp(topic, Secrets::MQTT_TOPIC_WEATHER_STATE) == 0) {
    parseWeatherMessage(message);
    return;
  }

  if (strcmp(topic, Secrets::MQTT_TOPIC_GARAGE_DOOR_STATE) == 0) {
    strlcpy(garageDoorState, message, sizeof(garageDoorState));
    updateAlarmLabels();
    return;
  }

  if (strcmp(topic, Secrets::ALARMO_MASTER_STATE_TOPIC) == 0) {
    strlcpy(masterAlarmState, message, sizeof(masterAlarmState));
    updateAlarmLabels();
    checkTriggeredState();
    return;
  }

  if (strcmp(topic, Secrets::ALARMO_GARAGE_STATE_TOPIC) == 0) {
    strlcpy(garageAlarmState, message, sizeof(garageAlarmState));
    updateAlarmLabels();
    checkTriggeredState();
  }
}

bool connectMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    updateMqttStatusLabel(false);
    return false;
  }

  if (mqttClient.connected()) {
    return true;
  }

  Serial.printf("[MQTT] Connecting to %s:%u\n", Secrets::MQTT_HOST, Secrets::MQTT_PORT);
  const bool connected =
      mqttClient.connect(Secrets::MQTT_CLIENT_ID, Secrets::MQTT_USERNAME, Secrets::MQTT_PASSWORD,
                         Secrets::MQTT_TOPIC_STATUS, 0, true, "offline");
  if (!connected) {
    Serial.printf("[MQTT] Connect failed, state=%d\n", mqttClient.state());
    updateMqttStatusLabel(false);
    return false;
  }

  Serial.println("[MQTT] Connected");
  mqttClient.publish(Secrets::MQTT_TOPIC_STATUS, "online", true);
  mqttClient.subscribe(Secrets::MQTT_TOPIC_DISPLAY_SET);
  mqttClient.subscribe(Secrets::MQTT_TOPIC_DISPLAY_TIMEOUT);
  mqttClient.subscribe(Secrets::MQTT_TOPIC_WEATHER_STATE);
  mqttClient.subscribe(Secrets::MQTT_TOPIC_GARAGE_DOOR_STATE);
  mqttClient.subscribe(Secrets::ALARMO_MASTER_STATE_TOPIC);
  mqttClient.subscribe(Secrets::ALARMO_GARAGE_STATE_TOPIC);
  updateMqttStatusLabel(true);
  return true;
}

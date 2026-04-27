#include <Arduino.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include <ctype.h>
#include <time.h>

#include "pin_config.h"
#include "secrets.h"

namespace {
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSpi(HSPI);
XPT2046_Touchscreen touch(PinConfig::TOUCH_CS_PIN, PinConfig::TOUCH_IRQ_PIN);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

lv_disp_draw_buf_t drawBuffer;
lv_color_t drawBufferPixels[PinConfig::SCREEN_WIDTH * 20];
lv_disp_drv_t displayDriver;
lv_indev_drv_t inputDriver;
lv_indev_t *touchInputDevice = nullptr;

lv_obj_t *statusLabel = nullptr;
lv_obj_t *masterStateLabel = nullptr;
lv_obj_t *garageStateLabel = nullptr;
lv_obj_t *masterStateDot = nullptr;
lv_obj_t *garageStateDot = nullptr;
lv_obj_t *garageDoorStateLabel = nullptr;
lv_obj_t *garageDoorStateDot = nullptr;
lv_obj_t *clockLabel = nullptr;
lv_obj_t *weatherLabel = nullptr;
lv_obj_t *masterPanel = nullptr;
lv_obj_t *garagePanel = nullptr;
lv_obj_t *garageDoorPanel = nullptr;
lv_obj_t *actionModal = nullptr;
lv_obj_t *actionStateLabel = nullptr;
lv_obj_t *pinPromptLabel = nullptr;
lv_obj_t *pinValueLabel = nullptr;
lv_obj_t *primaryActionButton = nullptr;
lv_obj_t *primaryActionLabel = nullptr;
lv_obj_t *armModesContainer = nullptr;
lv_obj_t *keypadContainer = nullptr;

// Phase 1: Screensaver
lv_obj_t *screensaverOverlay = nullptr;
lv_obj_t *screensaverClockLabel = nullptr;

// Phase 2: Toast
lv_obj_t *toastContainer = nullptr;
lv_obj_t *toastLabel = nullptr;
lv_timer_t *toastDismissTimer = nullptr;

// Phase 4: Triggered overlay
lv_obj_t *triggeredOverlay = nullptr;
lv_obj_t *triggeredLabel = nullptr;
lv_obj_t *triggeredSourceLabel = nullptr;
lv_timer_t *triggeredFlashTimer = nullptr;
bool triggeredDismissed = false;
bool triggeredFlashState = false;

uint32_t lastLvglTickMs = 0;
uint32_t lastMqttReconnectAttemptMs = 0;
uint32_t lastTouchLogMs = 0;
uint32_t displayWakeIgnoreTouchUntilMs = 0;
uint32_t lastClockUpdateMs = 0;
uint32_t lastTouchActivityMs = 0;
uint32_t displayTimeoutSec = 30;
char masterAlarmState[32] = "unknown";
char garageAlarmState[32] = "unknown";
char garageDoorState[32] = "unknown";
char weatherText[32] = "--";
char pinBuffer[16] = "";
bool timeConfigured = false;
bool otaStarted = false;

// Phase 0: Backlight PWM
constexpr uint8_t BACKLIGHT_LEDC_CHANNEL = 0;
constexpr uint32_t BACKLIGHT_LEDC_FREQ = 5000;
constexpr uint8_t BACKLIGHT_LEDC_RESOLUTION = 8;
constexpr uint8_t BRIGHTNESS_FULL = 255;
constexpr uint8_t BRIGHTNESS_DIM = 40;
constexpr uint8_t BRIGHTNESS_OFF = 0;
uint8_t currentBrightness = BRIGHTNESS_FULL;

enum class DisplayState {
  Active,
  Screensaver,
  Sleeping,
};

DisplayState displayState = DisplayState::Active;

enum class AlarmTarget {
  Master,
  Garage,
  GarageDoor,
};

enum class AlarmAction {
  None,
  Arm,
  Disarm,
};

enum class ArmMode {
  Home,
  Away,
  Night,
  Vacation,
};

AlarmTarget selectedTarget = AlarmTarget::Master;
AlarmAction pendingAction = AlarmAction::None;
ArmMode selectedArmMode = ArmMode::Home;

// Forward declarations
void closeActionModal();
void showToast(const char *message);
void checkTriggeredState();
void hideScreensaver();

void setStatusLabelText(const char *text, lv_color_t color) {
  if (statusLabel == nullptr) {
    return;
  }

  lv_label_set_text(statusLabel, text);
  lv_obj_set_style_text_color(statusLabel, color, LV_PART_MAIN);
}

void updateMqttStatusLabel(bool connected) {
  setStatusLabelText(connected ? "connected" : "disconnected",
                     connected ? lv_color_hex(0x81B29A) : lv_color_hex(0xE07A5F));
}

bool isDisarmedState(const char *state) {
  return strcmp(state, "disarmed") == 0;
}

bool isUnknownState(const char *state) {
  return strcmp(state, "unknown") == 0;
}

const char *targetName(AlarmTarget target) {
  switch (target) {
    case AlarmTarget::Master:
      return "Master Alarm";
    case AlarmTarget::Garage:
      return "Garage Alarm";
    case AlarmTarget::GarageDoor:
      return "Garage Door";
  }

  return "Unknown";
}

const char *targetState(AlarmTarget target) {
  switch (target) {
    case AlarmTarget::Master:
      return masterAlarmState;
    case AlarmTarget::Garage:
      return garageAlarmState;
    case AlarmTarget::GarageDoor:
      return garageDoorState;
  }

  return "unknown";
}

const char *armModeCommand(ArmMode mode) {
  switch (mode) {
    case ArmMode::Home:
      return "arm_home";
    case ArmMode::Away:
      return "arm_away";
    case ArmMode::Night:
      return "arm_night";
    case ArmMode::Vacation:
      return "arm_vacation";
  }

  return "arm_home";
}

const char *friendlyAlarmState(const char *state) {
  if (strcmp(state, "open") == 0) {
    return "Open";
  }
  if (strcmp(state, "closed") == 0) {
    return "Closed";
  }
  if (strcmp(state, "opening") == 0) {
    return "Opening";
  }
  if (strcmp(state, "closing") == 0) {
    return "Closing";
  }
  if (strcmp(state, "disarmed") == 0) {
    return "Disarmed";
  }
  if (strcmp(state, "arming") == 0) {
    return "Arming";
  }
  if (strcmp(state, "armed_home") == 0) {
    return "Armed Home";
  }
  if (strcmp(state, "armed_away") == 0) {
    return "Armed Away";
  }
  if (strcmp(state, "armed_night") == 0) {
    return "Armed Night";
  }
  if (strcmp(state, "armed_vacation") == 0) {
    return "Armed Vacation";
  }
  if (strcmp(state, "armed_custom_bypass") == 0) {
    return "Armed Custom";
  }
  if (strcmp(state, "pending") == 0) {
    return "Pending";
  }
  if (strcmp(state, "triggered") == 0) {
    return "Triggered";
  }
  if (strcmp(state, "unknown") == 0) {
    return "Waiting";
  }
  return state;
}

lv_color_t alarmStateColor(const char *state) {
  if (strcmp(state, "closed") == 0) {
    return lv_color_hex(0x7BD389);
  }
  if (strcmp(state, "open") == 0) {
    return lv_color_hex(0xE76F51);
  }
  if (strcmp(state, "opening") == 0 || strcmp(state, "closing") == 0) {
    return lv_color_hex(0xF2C14E);
  }
  if (strcmp(state, "disarmed") == 0) {
    return lv_color_hex(0x7BD389);
  }
  if (strcmp(state, "arming") == 0 || strcmp(state, "pending") == 0) {
    return lv_color_hex(0xF2C14E);
  }
  if (strcmp(state, "unknown") == 0) {
    return lv_color_hex(0xA8DADC);
  }
  return lv_color_hex(0xE76F51);
}

// Phase 3: Opacity pulse helper for state change animation
void opacityPulseExecCb(void *obj, int32_t value) {
  lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(value), LV_PART_MAIN);
}

void animateOpacityPulse(lv_obj_t *obj) {
  lv_anim_t fadeOut;
  lv_anim_init(&fadeOut);
  lv_anim_set_var(&fadeOut, obj);
  lv_anim_set_exec_cb(&fadeOut, opacityPulseExecCb);
  lv_anim_set_values(&fadeOut, LV_OPA_COVER, LV_OPA_30);
  lv_anim_set_time(&fadeOut, 150);
  lv_anim_set_path_cb(&fadeOut, lv_anim_path_ease_in);

  lv_anim_t fadeIn;
  lv_anim_init(&fadeIn);
  lv_anim_set_var(&fadeIn, obj);
  lv_anim_set_exec_cb(&fadeIn, opacityPulseExecCb);
  lv_anim_set_values(&fadeIn, LV_OPA_30, LV_OPA_COVER);
  lv_anim_set_time(&fadeIn, 150);
  lv_anim_set_delay(&fadeIn, 150);
  lv_anim_set_path_cb(&fadeIn, lv_anim_path_ease_out);

  lv_anim_start(&fadeOut);
  lv_anim_start(&fadeIn);
}

void setAlarmStateWidgets(lv_obj_t *label, lv_obj_t *dot, const char *state) {
  if (label == nullptr || dot == nullptr) {
    return;
  }

  lv_label_set_text(label, friendlyAlarmState(state));
  lv_obj_set_style_text_color(label, alarmStateColor(state), LV_PART_MAIN);
  lv_obj_set_style_bg_color(dot, alarmStateColor(state), LV_PART_MAIN);

  // Phase 3: Pulse animation on state change
  animateOpacityPulse(label);
  animateOpacityPulse(dot);
}

void updateAlarmLabels() {
  setAlarmStateWidgets(masterStateLabel, masterStateDot, masterAlarmState);
  setAlarmStateWidgets(garageStateLabel, garageStateDot, garageAlarmState);
  setAlarmStateWidgets(garageDoorStateLabel, garageDoorStateDot, garageDoorState);
}

// Phase 1: Screensaver clock update
void updateScreensaverClock() {
  if (screensaverClockLabel == nullptr) {
    return;
  }

  char timeText[16];
  struct tm timeInfo {};
  if (timeConfigured && getLocalTime(&timeInfo, 10)) {
    strftime(timeText, sizeof(timeText), "%H:%M", &timeInfo);
  } else {
    snprintf(timeText, sizeof(timeText), "--:--");
  }
  lv_label_set_text(screensaverClockLabel, timeText);
}

void updateClockDisplay() {
  if (clockLabel == nullptr) {
    return;
  }

  const uint32_t now = millis();
  if (lastClockUpdateMs != 0 && now - lastClockUpdateMs < 1000) {
    return;
  }
  lastClockUpdateMs = now;

  char timeText[16];
  struct tm timeInfo {};
  if (timeConfigured && getLocalTime(&timeInfo, 10)) {
    strftime(timeText, sizeof(timeText), "%H:%M", &timeInfo);
  } else {
    snprintf(timeText, sizeof(timeText), "--:--");
  }
  lv_label_set_text(clockLabel, timeText);

  updateScreensaverClock();
}

void updateWeatherDisplay() {
  if (weatherLabel == nullptr) {
    return;
  }

  lv_label_set_text(weatherLabel, weatherText);
}

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

bool mqttReady() {
  return WiFi.status() == WL_CONNECTED && mqttClient.connected();
}

// Phase 0: PWM backlight control
void setDisplayBrightness(uint8_t brightness) {
  ledcWrite(BACKLIGHT_LEDC_CHANNEL, brightness);
  currentBrightness = brightness;

  if (brightness == BRIGHTNESS_OFF) {
    displayState = DisplayState::Sleeping;
  }

  Serial.printf("[HW] Display brightness %u\n", brightness);
}

void resetIdleTimer() {
  lastTouchActivityMs = millis();
}

void wakeDisplay() {
  if (displayState == DisplayState::Active) {
    return;
  }

  if (displayState == DisplayState::Screensaver) {
    hideScreensaver();
  }

  setDisplayBrightness(BRIGHTNESS_FULL);
  displayState = DisplayState::Active;
  displayWakeIgnoreTouchUntilMs = millis() + 300;
  resetIdleTimer();
  Serial.println("[HW] Display woke");
}

void setupOta() {
  if (otaStarted || WiFi.status() != WL_CONNECTED) {
    return;
  }

  ArduinoOTA.setHostname(Secrets::OTA_HOSTNAME);
  ArduinoOTA.setPassword(Secrets::OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Start");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] End");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress * 100U) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]\n", error);
  });

  ArduinoOTA.begin();
  otaStarted = true;
  Serial.printf("[OTA] Ready at %s.local\n", Secrets::OTA_HOSTNAME);
}

void updatePinLabel() {
  if (pinValueLabel == nullptr) {
    return;
  }

  if (pinBuffer[0] == '\0') {
    lv_label_set_text(pinValueLabel, "_ _ _ _");
    return;
  }

  char masked[sizeof(pinBuffer) * 2] = "";
  for (size_t i = 0; i < strlen(pinBuffer); ++i) {
    strcat(masked, "*");
    if (i + 1 < strlen(pinBuffer)) {
      strcat(masked, " ");
    }
  }
  lv_label_set_text(pinValueLabel, masked);
}

void clearPinBuffer() {
  pinBuffer[0] = '\0';
  updatePinLabel();
}

// Phase 2: Toast functions
void hideToast() {
  if (toastContainer != nullptr) {
    lv_obj_add_flag(toastContainer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(toastContainer, LV_OPA_80, LV_PART_MAIN);
  }
}

void toastFadeExecCb(void *obj, int32_t value) {
  lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(value), LV_PART_MAIN);
}

void toastFadeReadyCb(lv_anim_t *anim) {
  hideToast();
}

void onToastDismiss(lv_timer_t *timer) {
  LV_UNUSED(timer);
  toastDismissTimer = nullptr;

  if (toastContainer == nullptr) {
    return;
  }

  lv_anim_t fadeOut;
  lv_anim_init(&fadeOut);
  lv_anim_set_var(&fadeOut, toastContainer);
  lv_anim_set_exec_cb(&fadeOut, toastFadeExecCb);
  lv_anim_set_values(&fadeOut, LV_OPA_80, LV_OPA_TRANSP);
  lv_anim_set_time(&fadeOut, 300);
  lv_anim_set_path_cb(&fadeOut, lv_anim_path_ease_in);
  lv_anim_set_ready_cb(&fadeOut, toastFadeReadyCb);
  lv_anim_start(&fadeOut);
}

void showToast(const char *message) {
  if (toastContainer == nullptr || toastLabel == nullptr) {
    return;
  }

  if (toastDismissTimer != nullptr) {
    lv_timer_del(toastDismissTimer);
    toastDismissTimer = nullptr;
  }

  lv_label_set_text(toastLabel, message);
  lv_obj_set_style_opa(toastContainer, LV_OPA_80, LV_PART_MAIN);
  lv_obj_clear_flag(toastContainer, LV_OBJ_FLAG_HIDDEN);

  toastDismissTimer = lv_timer_create(onToastDismiss, 2000, nullptr);
  lv_timer_set_repeat_count(toastDismissTimer, 1);
}

// Phase 3: Modal animation helpers
void modalOpacityExecCb(void *obj, int32_t value) {
  lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(value), LV_PART_MAIN);
}

void modalTranslateYExecCb(void *obj, int32_t value) {
  lv_obj_set_style_translate_y(static_cast<lv_obj_t *>(obj), value, LV_PART_MAIN);
}

void onCloseAnimReady(lv_anim_t *anim) {
  if (actionModal != nullptr) {
    lv_obj_add_flag(actionModal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(actionModal, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_translate_y(actionModal, 0, LV_PART_MAIN);
  }
}

void closeActionModal() {
  if (actionModal == nullptr) {
    return;
  }

  if (lv_obj_has_flag(actionModal, LV_OBJ_FLAG_HIDDEN)) {
    pendingAction = AlarmAction::None;
    clearPinBuffer();
    return;
  }

  // Phase 3: Animate close (fade out + slide down)
  lv_anim_t fadeOut;
  lv_anim_init(&fadeOut);
  lv_anim_set_var(&fadeOut, actionModal);
  lv_anim_set_exec_cb(&fadeOut, modalOpacityExecCb);
  lv_anim_set_values(&fadeOut, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_time(&fadeOut, 200);
  lv_anim_set_path_cb(&fadeOut, lv_anim_path_ease_in);
  lv_anim_set_ready_cb(&fadeOut, onCloseAnimReady);
  lv_anim_start(&fadeOut);

  lv_anim_t slideDown;
  lv_anim_init(&slideDown);
  lv_anim_set_var(&slideDown, actionModal);
  lv_anim_set_exec_cb(&slideDown, modalTranslateYExecCb);
  lv_anim_set_values(&slideDown, 0, 40);
  lv_anim_set_time(&slideDown, 200);
  lv_anim_set_path_cb(&slideDown, lv_anim_path_ease_in);
  lv_anim_start(&slideDown);

  pendingAction = AlarmAction::None;
  clearPinBuffer();
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

// Phase 4: Triggered overlay
void hideTriggeredOverlay() {
  if (triggeredOverlay != nullptr) {
    lv_obj_add_flag(triggeredOverlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (triggeredFlashTimer != nullptr) {
    lv_timer_del(triggeredFlashTimer);
    triggeredFlashTimer = nullptr;
  }
  triggeredFlashState = false;
}

void onTriggeredFlash(lv_timer_t *timer) {
  LV_UNUSED(timer);
  if (triggeredOverlay == nullptr) {
    return;
  }
  triggeredFlashState = !triggeredFlashState;
  lv_obj_set_style_bg_color(triggeredOverlay,
                             triggeredFlashState ? lv_color_hex(0xE76F51) : lv_color_hex(0xC0392B),
                             LV_PART_MAIN);
}

void showTriggeredOverlay() {
  if (triggeredOverlay == nullptr) {
    return;
  }

  wakeDisplay();

  // Show which alarm is triggered
  const char *source = "";
  if (strcmp(masterAlarmState, "triggered") == 0) {
    source = "Master Alarm";
  } else if (strcmp(garageAlarmState, "triggered") == 0) {
    source = "Garage Alarm";
  }
  if (triggeredSourceLabel != nullptr) {
    lv_label_set_text(triggeredSourceLabel, source);
  }

  lv_obj_clear_flag(triggeredOverlay, LV_OBJ_FLAG_HIDDEN);

  if (triggeredFlashTimer == nullptr) {
    triggeredFlashTimer = lv_timer_create(onTriggeredFlash, 500, nullptr);
  }
}

void checkTriggeredState() {
  const bool anyTriggered = strcmp(masterAlarmState, "triggered") == 0 ||
                             strcmp(garageAlarmState, "triggered") == 0;

  if (anyTriggered && !triggeredDismissed) {
    showTriggeredOverlay();
  } else if (!anyTriggered) {
    if (triggeredOverlay != nullptr && !lv_obj_has_flag(triggeredOverlay, LV_OBJ_FLAG_HIDDEN)) {
      hideTriggeredOverlay();
      showToast("Alarm cleared");
    }
    triggeredDismissed = false;
  }
}

void onTriggeredOverlayClicked(lv_event_t *event) {
  LV_UNUSED(event);
  triggeredDismissed = true;
  hideTriggeredOverlay();
}

// Phase 1: Screensaver
void showScreensaver() {
  // Close modal if open
  if (actionModal != nullptr && !lv_obj_has_flag(actionModal, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_flag(actionModal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(actionModal, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_translate_y(actionModal, 0, LV_PART_MAIN);
    pendingAction = AlarmAction::None;
    clearPinBuffer();
  }

  updateScreensaverClock();

  if (screensaverOverlay != nullptr) {
    lv_obj_clear_flag(screensaverOverlay, LV_OBJ_FLAG_HIDDEN);
  }

  setDisplayBrightness(BRIGHTNESS_DIM);
  displayState = DisplayState::Screensaver;
  Serial.println("[UI] Screensaver active");
}

void hideScreensaver() {
  if (screensaverOverlay != nullptr) {
    lv_obj_add_flag(screensaverOverlay, LV_OBJ_FLAG_HIDDEN);
  }
  resetIdleTimer();
  Serial.println("[UI] Screensaver hidden");
}

void checkIdleTimeout() {
  if (displayState != DisplayState::Active) {
    return;
  }
  if (displayTimeoutSec == 0) {
    return;
  }

  const uint32_t elapsed = millis() - lastTouchActivityMs;
  if (elapsed >= displayTimeoutSec * 1000UL) {
    showScreensaver();
  }
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

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.printf("[WIFI] Connecting to %s\n", Secrets::WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(Secrets::WIFI_SSID, Secrets::WIFI_PASSWORD);

  uint8_t retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    Serial.print(".");
    ++retries;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] Connected, IP=%s\n", WiFi.localIP().toString().c_str());
    configTzTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
    timeConfigured = true;
    setupOta();
  } else {
    Serial.println("[WIFI] Connection timed out");
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
      mqttClient.connect(Secrets::MQTT_CLIENT_ID, Secrets::MQTT_USERNAME, Secrets::MQTT_PASSWORD);
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

void ensureConnectivity() {
  if (WiFi.status() != WL_CONNECTED) {
    updateMqttStatusLabel(false);
    connectWifi();
    return;
  }

  if (mqttClient.connected()) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastMqttReconnectAttemptMs < 3000) {
    return;
  }

  lastMqttReconnectAttemptMs = now;
  connectMqtt();
}

void updatePrimaryActionButton() {
  if (primaryActionButton == nullptr || primaryActionLabel == nullptr) {
    return;
  }

  if (selectedTarget == AlarmTarget::GarageDoor) {
    const bool isClosed = strcmp(targetState(selectedTarget), "closed") == 0;
    lv_label_set_text(primaryActionLabel, isClosed ? "Open" : "Close");
    pendingAction = isClosed ? AlarmAction::Arm : AlarmAction::Disarm;
    if (armModesContainer != nullptr) {
      lv_obj_add_flag(armModesContainer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(primaryActionButton, LV_OBJ_FLAG_HIDDEN);
    if (pinValueLabel != nullptr) {
      lv_obj_add_flag(pinValueLabel, LV_OBJ_FLAG_HIDDEN);
    }
    if (keypadContainer != nullptr) {
      lv_obj_add_flag(keypadContainer, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  if (isDisarmedState(targetState(selectedTarget))) {
    pendingAction = AlarmAction::Arm;
    if (armModesContainer != nullptr) {
      lv_obj_clear_flag(armModesContainer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(primaryActionButton, LV_OBJ_FLAG_HIDDEN);
    if (pinValueLabel != nullptr) {
      lv_obj_add_flag(pinValueLabel, LV_OBJ_FLAG_HIDDEN);
    }
    if (keypadContainer != nullptr) {
      lv_obj_add_flag(keypadContainer, LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    lv_label_set_text(primaryActionLabel, "Disarm");
    pendingAction = AlarmAction::Disarm;
    if (armModesContainer != nullptr) {
      lv_obj_add_flag(armModesContainer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(primaryActionButton, LV_OBJ_FLAG_HIDDEN);
    if (pinValueLabel != nullptr) {
      lv_obj_clear_flag(pinValueLabel, LV_OBJ_FLAG_HIDDEN);
    }
    if (keypadContainer != nullptr) {
      lv_obj_clear_flag(keypadContainer, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void openActionModal(AlarmTarget target) {
  selectedTarget = target;
  clearPinBuffer();

  lv_label_set_text(actionStateLabel, friendlyAlarmState(targetState(target)));
  updatePrimaryActionButton();
  if (selectedTarget == AlarmTarget::GarageDoor) {
    lv_label_set_text(pinPromptLabel, pendingAction == AlarmAction::Arm ? "Open garage door"
                                                                        : "Close garage door");
  } else if (pendingAction == AlarmAction::Arm) {
    lv_label_set_text(pinPromptLabel, "Choose arm mode");
  } else {
    lv_label_set_text(pinPromptLabel, "Enter PIN to disarm");
  }

  // Phase 3: Set initial state for animation
  lv_obj_set_style_opa(actionModal, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_translate_y(actionModal, 40, LV_PART_MAIN);
  lv_obj_clear_flag(actionModal, LV_OBJ_FLAG_HIDDEN);

  // Fade in
  lv_anim_t fadeIn;
  lv_anim_init(&fadeIn);
  lv_anim_set_var(&fadeIn, actionModal);
  lv_anim_set_exec_cb(&fadeIn, modalOpacityExecCb);
  lv_anim_set_values(&fadeIn, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_time(&fadeIn, 250);
  lv_anim_set_path_cb(&fadeIn, lv_anim_path_ease_out);
  lv_anim_start(&fadeIn);

  // Slide up
  lv_anim_t slideUp;
  lv_anim_init(&slideUp);
  lv_anim_set_var(&slideUp, actionModal);
  lv_anim_set_exec_cb(&slideUp, modalTranslateYExecCb);
  lv_anim_set_values(&slideUp, 40, 0);
  lv_anim_set_time(&slideUp, 250);
  lv_anim_set_path_cb(&slideUp, lv_anim_path_ease_out);
  lv_anim_start(&slideUp);
}

void onAlarmPanelPressed(lv_event_t *event) {
  const intptr_t userData = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  switch (userData) {
    case 0:
      openActionModal(AlarmTarget::Master);
      break;
    case 1:
      openActionModal(AlarmTarget::Garage);
      break;
    case 2:
      openActionModal(AlarmTarget::GarageDoor);
      break;
  }
}

void onPrimaryActionPressed(lv_event_t *event) {
  LV_UNUSED(event);
  if (selectedTarget == AlarmTarget::GarageDoor) {
    Serial.printf("[UI] Executing garage door %s\n",
                  pendingAction == AlarmAction::Arm ? "open" : "close");
    executeAlarmAction(selectedTarget, pendingAction, "");
    closeActionModal();
    return;
  }

  if (pinBuffer[0] == '\0') {
    showToast("Enter PIN first");
    return;
  }

  Serial.printf("[UI] Executing %s for %s\n",
                pendingAction == AlarmAction::Arm ? "arm" : "disarm",
                selectedTarget == AlarmTarget::Master ? "master" : "garage");
  executeAlarmAction(selectedTarget, pendingAction, pinBuffer);
  closeActionModal();
}

void onPinDigitPressed(lv_event_t *event) {
  LV_UNUSED(event);
  if (pendingAction == AlarmAction::None) {
    return;
  }

  const char *digit = lv_label_get_text(lv_obj_get_child(lv_event_get_target(event), 0));
  const size_t currentLength = strlen(pinBuffer);
  if (currentLength >= sizeof(pinBuffer) - 1) {
    return;
  }

  pinBuffer[currentLength] = digit[0];
  pinBuffer[currentLength + 1] = '\0';
  updatePinLabel();
}

void onArmModePressed(lv_event_t *event) {
  const intptr_t userData = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  selectedArmMode = static_cast<ArmMode>(userData);

  const char *modeName = nullptr;
  switch (selectedArmMode) {
    case ArmMode::Home:
      modeName = "home";
      break;
    case ArmMode::Away:
      modeName = "away";
      break;
    case ArmMode::Night:
      modeName = "night";
      break;
    case ArmMode::Vacation:
      modeName = "vacation";
      break;
  }

  if (pinPromptLabel != nullptr && modeName != nullptr) {
    char prompt[32];
    snprintf(prompt, sizeof(prompt), "Arming %s", modeName);
    lv_label_set_text(pinPromptLabel, prompt);
  }

  Serial.printf("[UI] Executing arm %s for %s\n", modeName != nullptr ? modeName : "unknown",
                selectedTarget == AlarmTarget::Master ? "master" : "garage");
  executeAlarmAction(selectedTarget, AlarmAction::Arm, "");
  closeActionModal();
}

void onPinBackspacePressed(lv_event_t *event) {
  LV_UNUSED(event);
  const size_t currentLength = strlen(pinBuffer);
  if (currentLength == 0) {
    return;
  }

  pinBuffer[currentLength - 1] = '\0';
  updatePinLabel();
}

void onPinCancelPressed(lv_event_t *event) {
  LV_UNUSED(event);
  Serial.println("[UI] Action cancelled");
  closeActionModal();
}

void onPinSubmitPressed(lv_event_t *event) {
  LV_UNUSED(event);
  if (pendingAction == AlarmAction::None || pinBuffer[0] == '\0') {
    Serial.println("[UI] PIN submit ignored, action or code missing");
    showToast("Enter PIN first");
    return;
  }

  Serial.printf("[UI] Executing %s for %s\n",
                pendingAction == AlarmAction::Arm ? "arm" : "disarm",
                selectedTarget == AlarmTarget::Master ? "master" : "garage");
  executeAlarmAction(selectedTarget, pendingAction, pinBuffer);
  closeActionModal();
}

void displayFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  const uint32_t width = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  const uint32_t height = static_cast<uint32_t>(area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, width, height);
  tft.pushColors(reinterpret_cast<uint16_t *>(&colorP->full), width * height, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

bool mapTouchPoint(const TS_Point &rawPoint, lv_point_t &mappedPoint) {
  if (rawPoint.x < PinConfig::TOUCH_RAW_MIN_X || rawPoint.x > PinConfig::TOUCH_RAW_MAX_X ||
      rawPoint.y < PinConfig::TOUCH_RAW_MIN_Y || rawPoint.y > PinConfig::TOUCH_RAW_MAX_Y) {
    return false;
  }

  const int16_t rawX = PinConfig::TOUCH_SWAP_XY ? rawPoint.y : rawPoint.x;
  const int16_t rawY = PinConfig::TOUCH_SWAP_XY ? rawPoint.x : rawPoint.y;

  const int32_t mappedX =
      PinConfig::TOUCH_INVERT_X
          ? map(rawX, PinConfig::TOUCH_RAW_MAX_Y, PinConfig::TOUCH_RAW_MIN_Y, 0,
                PinConfig::SCREEN_WIDTH - 1)
          : map(rawX, PinConfig::TOUCH_RAW_MIN_Y, PinConfig::TOUCH_RAW_MAX_Y, 0,
                PinConfig::SCREEN_WIDTH - 1);

  const int32_t mappedY =
      PinConfig::TOUCH_INVERT_Y
          ? map(rawY, PinConfig::TOUCH_RAW_MAX_X, PinConfig::TOUCH_RAW_MIN_X, 0,
                PinConfig::SCREEN_HEIGHT - 1)
          : map(rawY, PinConfig::TOUCH_RAW_MIN_X, PinConfig::TOUCH_RAW_MAX_X, 0,
                PinConfig::SCREEN_HEIGHT - 1);

  mappedPoint.x = constrain(mappedX, 0, PinConfig::SCREEN_WIDTH - 1);
  mappedPoint.y = constrain(mappedY, 0, PinConfig::SCREEN_HEIGHT - 1);
  return true;
}

void touchRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  LV_UNUSED(drv);

  if (!touch.touched()) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  const TS_Point rawPoint = touch.getPoint();
  lv_point_t mappedPoint{};

  // Phase 1: 3-state display handling
  if (displayState == DisplayState::Sleeping) {
    Serial.println("[TOUCH] Wake from sleep");
    wakeDisplay();
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  if (displayState == DisplayState::Screensaver) {
    Serial.println("[TOUCH] Wake from screensaver");
    wakeDisplay();
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  if (millis() < displayWakeIgnoreTouchUntilMs) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  if (!mapTouchPoint(rawPoint, mappedPoint)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  resetIdleTimer();

  const uint32_t now = millis();
  if (now - lastTouchLogMs >= 150) {
    Serial.printf("[TOUCH] raw=(%d,%d,%d) mapped=(%d,%d)\n", rawPoint.x, rawPoint.y, rawPoint.z,
                  mappedPoint.x, mappedPoint.y);
    lastTouchLogMs = now;
  }

  data->state = LV_INDEV_STATE_PRESSED;
  data->point = mappedPoint;
}

lv_obj_t *createActionButton(lv_obj_t *parent, const char *labelText,
                             lv_event_cb_t eventCallback) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_size(button, LV_PCT(48), 44);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x111111), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, lv_color_hex(0xF4F1DE), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
  lv_obj_add_event_cb(button, eventCallback, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, labelText);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(0xF4F1DE), LV_PART_MAIN);
  lv_obj_center(label);

  return button;
}

lv_obj_t *createKeyButton(lv_obj_t *parent, const char *labelText, lv_event_cb_t eventCallback) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_size(button, 64, 42);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x111111), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, lv_color_hex(0xF4F1DE), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
  lv_obj_add_event_cb(button, eventCallback, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, labelText);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(0xF4F1DE), LV_PART_MAIN);
  lv_obj_center(label);

  return button;
}

lv_obj_t *createArmModeButton(lv_obj_t *parent, const char *labelText, ArmMode mode) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_size(button, LV_PCT(48), 40);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x111111), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, lv_color_hex(0xF4F1DE), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
  lv_obj_add_event_cb(button, onArmModePressed, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(mode)));

  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, labelText);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(0xF4F1DE), LV_PART_MAIN);
  lv_obj_center(label);

  return button;
}

lv_obj_t *createAlarmPanel(lv_obj_t *parent, const char *titleText, lv_obj_t **stateLabelOut,
                           lv_obj_t **stateDotOut, AlarmTarget target) {
  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_set_width(panel, LV_PCT(100));
  lv_obj_set_height(panel, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x24333E), LV_PART_MAIN);
  lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(panel, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_left(panel, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_right(panel, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_top(panel, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(panel, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(panel, 4, LV_PART_MAIN);
  lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *title = lv_label_create(panel);
  lv_label_set_text(title, titleText);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(0xF4F1DE), LV_PART_MAIN);

  lv_obj_t *stateRow = lv_obj_create(panel);
  lv_obj_set_width(stateRow, LV_PCT(100));
  lv_obj_set_height(stateRow, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(stateRow, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(stateRow, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(stateRow, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(stateRow, 10, LV_PART_MAIN);
  lv_obj_set_layout(stateRow, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(stateRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(stateRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *stateDot = lv_obj_create(stateRow);
  lv_obj_remove_style_all(stateDot);
  lv_obj_set_size(stateDot, 12, 12);
  lv_obj_set_style_radius(stateDot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(stateDot, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *stateLabel = lv_label_create(stateRow);
  lv_label_set_text(stateLabel, "Waiting");
  lv_obj_set_style_text_font(stateLabel, &lv_font_montserrat_18, LV_PART_MAIN);
  *stateLabelOut = stateLabel;
  *stateDotOut = stateDot;

  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(panel, onAlarmPanelPressed, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(target)));

  return panel;
}

void setupActionModal(lv_obj_t *parent) {
  actionModal = lv_obj_create(parent);
  lv_obj_set_size(actionModal, PinConfig::SCREEN_WIDTH, PinConfig::SCREEN_HEIGHT);
  lv_obj_center(actionModal);
  lv_obj_add_flag(actionModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_color(actionModal, lv_color_hex(0x13202A), LV_PART_MAIN);
  lv_obj_set_style_border_width(actionModal, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(actionModal, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(actionModal, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_right(actionModal, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_top(actionModal, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(actionModal, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(actionModal, 5, LV_PART_MAIN);
  lv_obj_clear_flag(actionModal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(actionModal, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(actionModal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(actionModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  actionStateLabel = lv_label_create(actionModal);
  lv_obj_set_width(actionStateLabel, LV_PCT(100));
  lv_label_set_long_mode(actionStateLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(actionStateLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_font(actionStateLabel, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(actionStateLabel, lv_color_hex(0xA8DADC), LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(actionStateLabel, 2, LV_PART_MAIN);

  lv_obj_t *actionRow = lv_obj_create(actionModal);
  lv_obj_set_width(actionRow, LV_PCT(100));
  lv_obj_set_height(actionRow, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(actionRow, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(actionRow, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(actionRow, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(actionRow, 8, LV_PART_MAIN);
  lv_obj_set_layout(actionRow, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(actionRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actionRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  primaryActionButton = createActionButton(actionRow, "Arm", onPrimaryActionPressed);
  primaryActionLabel = lv_obj_get_child(primaryActionButton, 0);

  lv_obj_t *closeButton = createActionButton(actionRow, "X", onPinCancelPressed);
  lv_obj_t *closeLabel = lv_obj_get_child(closeButton, 0);
  lv_obj_set_style_text_font(closeLabel, &lv_font_montserrat_18, LV_PART_MAIN);
  lv_obj_set_style_pad_top(actionRow, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(actionRow, 2, LV_PART_MAIN);

  pinPromptLabel = lv_label_create(actionModal);
  lv_label_set_text(pinPromptLabel, "Enter PIN");
  lv_obj_set_style_text_font(pinPromptLabel, &lv_font_montserrat_16, LV_PART_MAIN);

  armModesContainer = lv_obj_create(actionModal);
  lv_obj_set_width(armModesContainer, LV_PCT(100));
  lv_obj_set_height(armModesContainer, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(armModesContainer, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(armModesContainer, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(armModesContainer, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(armModesContainer, 6, LV_PART_MAIN);
  lv_obj_clear_flag(armModesContainer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(armModesContainer, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(armModesContainer, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(armModesContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  createArmModeButton(armModesContainer, "Home", ArmMode::Home);
  createArmModeButton(armModesContainer, "Away", ArmMode::Away);
  createArmModeButton(armModesContainer, "Night", ArmMode::Night);
  createArmModeButton(armModesContainer, "Vacation", ArmMode::Vacation);

  pinValueLabel = lv_label_create(actionModal);
  lv_obj_set_style_text_font(pinValueLabel, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(pinValueLabel, lv_color_hex(0xF2CC8F), LV_PART_MAIN);
  updatePinLabel();

  keypadContainer = lv_obj_create(actionModal);
  lv_obj_set_width(keypadContainer, LV_PCT(100));
  lv_obj_set_height(keypadContainer, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(keypadContainer, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(keypadContainer, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(keypadContainer, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(keypadContainer, 4, LV_PART_MAIN);
  lv_obj_clear_flag(keypadContainer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(keypadContainer, LV_LAYOUT_GRID);

  static lv_coord_t columnDesc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t rowDesc[] = {38, 38, 38, 38, LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(keypadContainer, columnDesc, rowDesc);

  const char *digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", "OK"};
  lv_event_cb_t handlers[] = {onPinDigitPressed, onPinDigitPressed, onPinDigitPressed,
                              onPinDigitPressed, onPinDigitPressed, onPinDigitPressed,
                              onPinDigitPressed, onPinDigitPressed, onPinDigitPressed,
                              onPinBackspacePressed, onPinDigitPressed, onPinSubmitPressed};

  for (uint8_t i = 0; i < 12; ++i) {
    lv_obj_t *button = createKeyButton(keypadContainer, digits[i], handlers[i]);
    lv_obj_set_grid_cell(button, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
  }
}

// Phase 1: Screensaver setup
void setupScreensaver(lv_obj_t *parent) {
  screensaverOverlay = lv_obj_create(parent);
  lv_obj_set_size(screensaverOverlay, PinConfig::SCREEN_WIDTH, PinConfig::SCREEN_HEIGHT);
  lv_obj_center(screensaverOverlay);
  lv_obj_set_style_bg_color(screensaverOverlay, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screensaverOverlay, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(screensaverOverlay, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(screensaverOverlay, 0, LV_PART_MAIN);
  lv_obj_clear_flag(screensaverOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(screensaverOverlay, LV_OBJ_FLAG_HIDDEN);

  screensaverClockLabel = lv_label_create(screensaverOverlay);
  lv_label_set_text(screensaverClockLabel, "--:--");
  lv_obj_set_style_text_font(screensaverClockLabel, &lv_font_montserrat_48, LV_PART_MAIN);
  lv_obj_set_style_text_color(screensaverClockLabel, lv_color_hex(0xF4F1DE), LV_PART_MAIN);
  lv_obj_center(screensaverClockLabel);
}

// Phase 2: Toast setup
void setupToast(lv_obj_t *parent) {
  toastContainer = lv_obj_create(parent);
  lv_obj_set_size(toastContainer, 220, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(toastContainer, lv_color_hex(0x24333E), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(toastContainer, LV_OPA_80, LV_PART_MAIN);
  lv_obj_set_style_border_width(toastContainer, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(toastContainer, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_all(toastContainer, 10, LV_PART_MAIN);
  lv_obj_align(toastContainer, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_clear_flag(toastContainer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(toastContainer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(toastContainer, LV_OBJ_FLAG_HIDDEN);

  toastLabel = lv_label_create(toastContainer);
  lv_label_set_text(toastLabel, "");
  lv_obj_set_style_text_font(toastLabel, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(toastLabel, lv_color_hex(0xF4F1DE), LV_PART_MAIN);
  lv_obj_set_style_text_align(toastLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_width(toastLabel, LV_PCT(100));
  lv_obj_center(toastLabel);
}

// Phase 4: Triggered overlay setup
void setupTriggeredOverlay(lv_obj_t *parent) {
  triggeredOverlay = lv_obj_create(parent);
  lv_obj_set_size(triggeredOverlay, PinConfig::SCREEN_WIDTH, PinConfig::SCREEN_HEIGHT);
  lv_obj_center(triggeredOverlay);
  lv_obj_set_style_bg_color(triggeredOverlay, lv_color_hex(0xC0392B), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(triggeredOverlay, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(triggeredOverlay, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(triggeredOverlay, 0, LV_PART_MAIN);
  lv_obj_clear_flag(triggeredOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(triggeredOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(triggeredOverlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(triggeredOverlay, onTriggeredOverlayClicked, LV_EVENT_CLICKED, nullptr);

  // Layout: flex column centered
  lv_obj_set_layout(triggeredOverlay, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(triggeredOverlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(triggeredOverlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(triggeredOverlay, 12, LV_PART_MAIN);

  triggeredLabel = lv_label_create(triggeredOverlay);
  lv_label_set_text(triggeredLabel, "ALARM");
  lv_obj_set_style_text_font(triggeredLabel, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_set_style_text_color(triggeredLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

  triggeredSourceLabel = lv_label_create(triggeredOverlay);
  lv_label_set_text(triggeredSourceLabel, "");
  lv_obj_set_style_text_font(triggeredSourceLabel, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(triggeredSourceLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

  lv_obj_t *hintLabel = lv_label_create(triggeredOverlay);
  lv_label_set_text(hintLabel, "Tap to dismiss");
  lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(hintLabel, lv_color_hex(0xFFCCCC), LV_PART_MAIN);
}

void setupUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *container = lv_obj_create(screen);
  lv_obj_set_size(container, PinConfig::SCREEN_WIDTH - 16, PinConfig::SCREEN_HEIGHT - 16);
  lv_obj_center(container);
  lv_obj_set_style_radius(container, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(container, lv_color_hex(0x1B2A34), LV_PART_MAIN);
  lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(container, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_right(container, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_top(container, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(container, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(container, 8, LV_PART_MAIN);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *topRow = lv_obj_create(container);
  lv_obj_set_width(topRow, LV_PCT(100));
  lv_obj_set_height(topRow, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(topRow, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(topRow, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(topRow, 0, LV_PART_MAIN);
  lv_obj_set_layout(topRow, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  clockLabel = lv_label_create(topRow);
  lv_label_set_text(clockLabel, "--:--");
  lv_obj_set_style_text_font(clockLabel, &lv_font_montserrat_18, LV_PART_MAIN);
  lv_obj_set_style_text_color(clockLabel, lv_color_hex(0xF4F1DE), LV_PART_MAIN);

  weatherLabel = lv_label_create(topRow);
  lv_label_set_text(weatherLabel, weatherText);
  lv_obj_set_style_text_font(weatherLabel, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(weatherLabel, lv_color_hex(0xA8DADC), LV_PART_MAIN);

  statusLabel = lv_label_create(container);
  lv_label_set_text(statusLabel, "");
  lv_obj_add_flag(statusLabel, LV_OBJ_FLAG_HIDDEN);

  masterPanel =
      createAlarmPanel(container, "Master", &masterStateLabel, &masterStateDot, AlarmTarget::Master);
  garagePanel =
      createAlarmPanel(container, "Garage", &garageStateLabel, &garageStateDot, AlarmTarget::Garage);
  garageDoorPanel = createAlarmPanel(container, "Garage Door", &garageDoorStateLabel,
                                     &garageDoorStateDot, AlarmTarget::GarageDoor);
  LV_UNUSED(masterPanel);
  LV_UNUSED(garagePanel);

  updateAlarmLabels();
  updateClockDisplay();
  updateWeatherDisplay();

  // Z-order: action modal, screensaver, toast, triggered overlay
  setupActionModal(screen);
  setupScreensaver(screen);
  setupToast(screen);
  setupTriggeredOverlay(screen);

  Serial.println("[UI] UI initialized");
}

void setupDisplay() {
  // Phase 0: PWM backlight
  ledcSetup(BACKLIGHT_LEDC_CHANNEL, BACKLIGHT_LEDC_FREQ, BACKLIGHT_LEDC_RESOLUTION);
  ledcAttachPin(PinConfig::DISPLAY_LED, BACKLIGHT_LEDC_CHANNEL);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  setDisplayBrightness(BRIGHTNESS_FULL);
  displayState = DisplayState::Active;

  Serial.println("[HW] Display initialized");
}

void setupTouch() {
  touchSpi.begin(PinConfig::TOUCH_SCLK_PIN, PinConfig::TOUCH_MISO_PIN,
                 PinConfig::TOUCH_MOSI_PIN, PinConfig::TOUCH_CS_PIN);
  touch.begin(touchSpi);
  touch.setRotation(0);

  Serial.println("[HW] Touch initialized");
  Serial.println("[HW] Touch calibration: adjust TOUCH_SWAP_XY / TOUCH_INVERT_X / TOUCH_INVERT_Y");
  Serial.println("[HW] Touch calibration: adjust TOUCH_RAW_MIN/MAX values to align edges");
}

void setupLvgl() {
  lv_init();

  lv_disp_draw_buf_init(&drawBuffer, drawBufferPixels, nullptr,
                        PinConfig::SCREEN_WIDTH * 20);

  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = PinConfig::SCREEN_WIDTH;
  displayDriver.ver_res = PinConfig::SCREEN_HEIGHT;
  displayDriver.flush_cb = displayFlush;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  lv_indev_drv_init(&inputDriver);
  inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = touchRead;
  touchInputDevice = lv_indev_drv_register(&inputDriver);
  LV_UNUSED(touchInputDevice);

  setupUi();
}

void loopLvgl() {
  updateClockDisplay();
  checkIdleTimeout();
  const uint32_t now = millis();
  lv_tick_inc(now - lastLvglTickMs);
  lastLvglTickMs = now;
  lv_timer_handler();
  delay(5);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("[BOOT] ESP32 home keypad proof of concept");

  setupDisplay();
  setupTouch();
  setupLvgl();
  mqttClient.setServer(Secrets::MQTT_HOST, Secrets::MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  lastLvglTickMs = millis();
  lastTouchActivityMs = millis();

  // WiFi and MQTT are initialized after the UI so connection state can be shown on-screen.
  // Add subscriptions and inbound message handling after connectMqtt() when needed.
  connectWifi();
  connectMqtt();
}

void loop() {
  ensureConnectivity();
  if (otaStarted) {
    ArduinoOTA.handle();
  }
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  loopLvgl();
}

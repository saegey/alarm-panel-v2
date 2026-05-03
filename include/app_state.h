#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

// --- Enums ---

enum class DisplayState {
  Active,
  Screensaver,
  Sleeping,
};

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

enum class AlarmLifecycleState {
  Unknown,
  Disarmed,
  Arming,
  ArmedHome,
  ArmedAway,
  ArmedNight,
  ArmedVacation,
  ArmedCustomBypass,
  Pending,
  Triggered,
  Open,
  Closed,
  Opening,
  Closing,
};

enum class ArmMode {
  Home,
  Away,
  Night,
  Vacation,
};

// --- Shared constants ---

constexpr uint8_t BRIGHTNESS_FULL = 255;
constexpr uint8_t BRIGHTNESS_DIM = 40;
constexpr uint8_t BRIGHTNESS_OFF = 0;

// --- Extern globals: main.cpp ---

extern PubSubClient mqttClient;
extern Preferences preferences;
extern bool timeConfigured;

// --- Extern globals: display.cpp ---

extern TFT_eSPI tft;
extern uint8_t currentBrightness;
extern DisplayState displayState;
extern uint32_t displayTimeoutSec;
extern uint32_t lastTouchActivityMs;
extern uint32_t displayWakeIgnoreTouchUntilMs;
extern uint32_t lastLvglTickMs;

// --- Extern globals: mqtt.cpp ---

extern char masterAlarmState[32];
extern char garageAlarmState[32];
extern char garageDoorState[32];
extern char weatherText[32];

// --- Extern globals: ui.cpp ---

extern ArmMode selectedArmMode;

// --- Function declarations: main.cpp ---

void feedWatchdog();
void saveSettings();

// --- Function declarations: display.cpp ---

void setDisplayBrightness(uint8_t brightness);
void wakeDisplay();
void resetIdleTimer();
void checkIdleTimeout();
void setupDisplay();
void setupTouch();
void displayFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP);
void touchRead(lv_indev_drv_t *drv, lv_indev_data_t *data);

// --- Function declarations: mqtt.cpp ---

bool mqttReady();
bool connectMqtt();
void mqttCallback(char *topic, byte *payload, unsigned int length);
bool executeAlarmAction(AlarmTarget target, AlarmAction action, const char *code);

// --- Function declarations: ui.cpp ---

void setupLvgl();
void loopLvgl();
void showToast(const char *message);
void showScreensaver();
void hideScreensaver();
void checkTriggeredState();
void updateAlarmLabels();
void updateWeatherDisplay();
void updateMqttStatusLabel(bool connected);
void setStatusLabelText(const char *text, lv_color_t color);
const char *friendlyAlarmState(const char *state);
lv_color_t alarmStateColor(const char *state);
const char *targetState(AlarmTarget target);
const char *targetName(AlarmTarget target);
const char *armModeCommand(ArmMode mode);
bool isDisarmedState(const char *state);
AlarmLifecycleState parseAlarmLifecycleState(const char *state);
bool isTriggeredState(AlarmLifecycleState state);
AlarmAction nextActionForTargetState(AlarmTarget target, AlarmLifecycleState state);

#if defined(WOKWI_SIM)
void handleSimInputChar(char c);
#endif

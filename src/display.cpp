#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

#include "app_state.h"
#include "pin_config.h"

// --- Globals owned by this module ---

TFT_eSPI tft = TFT_eSPI();
uint8_t currentBrightness = BRIGHTNESS_FULL;
DisplayState displayState = DisplayState::Active;
uint32_t displayTimeoutSec = 30;
uint32_t lastTouchActivityMs = 0;
uint32_t displayWakeIgnoreTouchUntilMs = 0;
uint32_t lastLvglTickMs = 0;

// --- File-local state ---

namespace {
SPIClass touchSpi(HSPI);
XPT2046_Touchscreen touch(PinConfig::TOUCH_CS_PIN, PinConfig::TOUCH_IRQ_PIN);
uint32_t lastTouchLogMs = 0;
uint32_t lastTouchDebugMs = 0;
lv_point_t lastTouchPoint{};
bool hasLastTouchPoint = false;
uint8_t stableTouchSamples = 0;
bool touchSessionActive = false;

constexpr uint8_t BACKLIGHT_LEDC_CHANNEL = 0;
constexpr uint32_t BACKLIGHT_LEDC_FREQ = 5000;
constexpr uint8_t BACKLIGHT_LEDC_RESOLUTION = 8;
}  // namespace

// --- Functions ---

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

void setupDisplay() {
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
#if defined(WOKWI_SIM)
  Serial.println("[SIM] Touch disabled in Wokwi: use serial controls instead");
  return;
#endif
  pinMode(PinConfig::TOUCH_IRQ_PIN, INPUT_PULLUP);
  touchSpi.begin(PinConfig::TOUCH_SCLK_PIN, PinConfig::TOUCH_MISO_PIN,
                 PinConfig::TOUCH_MOSI_PIN, PinConfig::TOUCH_CS_PIN);
  touch.begin(touchSpi);
  touch.setRotation(0);

  Serial.println("[HW] Touch initialized");
  Serial.println("[HW] Touch calibration: adjust TOUCH_SWAP_XY / TOUCH_INVERT_X / TOUCH_INVERT_Y");
  Serial.println("[HW] Touch calibration: adjust TOUCH_RAW_MIN/MAX values to align edges");
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

namespace {
bool mapTouchPoint(const TS_Point &rawPoint, lv_point_t &mappedPoint) {
  const int16_t axisX = PinConfig::TOUCH_SWAP_XY ? rawPoint.y : rawPoint.x;
  const int16_t axisY = PinConfig::TOUCH_SWAP_XY ? rawPoint.x : rawPoint.y;

  const uint16_t axisXMin = PinConfig::TOUCH_SWAP_XY ? PinConfig::TOUCH_RAW_MIN_Y
                                                     : PinConfig::TOUCH_RAW_MIN_X;
  const uint16_t axisXMax = PinConfig::TOUCH_SWAP_XY ? PinConfig::TOUCH_RAW_MAX_Y
                                                     : PinConfig::TOUCH_RAW_MAX_X;
  const uint16_t axisYMin = PinConfig::TOUCH_SWAP_XY ? PinConfig::TOUCH_RAW_MIN_X
                                                     : PinConfig::TOUCH_RAW_MIN_Y;
  const uint16_t axisYMax = PinConfig::TOUCH_SWAP_XY ? PinConfig::TOUCH_RAW_MAX_X
                                                     : PinConfig::TOUCH_RAW_MAX_Y;

  if (axisX < axisXMin || axisX > axisXMax || axisY < axisYMin || axisY > axisYMax) {
    return false;
  }

  const int32_t mappedX =
      PinConfig::TOUCH_INVERT_X
          ? map(axisX, axisXMax, axisXMin, 0,
                PinConfig::SCREEN_WIDTH - 1)
          : map(axisX, axisXMin, axisXMax, 0,
                PinConfig::SCREEN_WIDTH - 1);

  const int32_t mappedY =
      PinConfig::TOUCH_INVERT_Y
          ? map(axisY, axisYMax, axisYMin, 0,
                PinConfig::SCREEN_HEIGHT - 1)
          : map(axisY, axisYMin, axisYMax, 0,
                PinConfig::SCREEN_HEIGHT - 1);

  mappedPoint.x = constrain(mappedX, 0, PinConfig::SCREEN_WIDTH - 1);
  mappedPoint.y = constrain(mappedY, 0, PinConfig::SCREEN_HEIGHT - 1);
  return true;
}
}  // namespace

void touchRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  LV_UNUSED(drv);

#if defined(WOKWI_SIM)
  data->state = LV_INDEV_STATE_RELEASED;
  return;
#endif
  const uint32_t now = millis();
  const int irqLevel = digitalRead(PinConfig::TOUCH_IRQ_PIN);
  const bool touched = touch.touched();

  if (PinConfig::TOUCH_DEBUG_MODE && now - lastTouchDebugMs >= PinConfig::TOUCH_DEBUG_INTERVAL_MS) {
    if (touched) {
      const TS_Point debugPoint = touch.getPoint();
      Serial.printf("[TOUCHDBG] irq=%d touched=%d raw=(%d,%d,%d) stable=%u\n", irqLevel, touched,
                    debugPoint.x, debugPoint.y, debugPoint.z, stableTouchSamples);
    } else {
      Serial.printf("[TOUCHDBG] irq=%d touched=%d stable=%u\n", irqLevel, touched, stableTouchSamples);
    }
    lastTouchDebugMs = now;
  }

  // IRQ is active-low for XPT2046. Treat as a hint; some boards/wiring setups
  // keep this line unreliable while touched() still reports valid state.
  if (irqLevel == HIGH && !touched) {
    hasLastTouchPoint = false;
    stableTouchSamples = 0;
    touchSessionActive = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  if (!touched) {
    hasLastTouchPoint = false;
    stableTouchSamples = 0;
    touchSessionActive = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  const TS_Point rawPoint = touch.getPoint();
  if (rawPoint.z < PinConfig::TOUCH_PRESSURE_MIN) {
    stableTouchSamples = 0;
    touchSessionActive = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  lv_point_t mappedPoint{};

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
    stableTouchSamples = 0;
    touchSessionActive = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  // Reject single-frame spikes from the resistive controller when signal is noisy.
  if (hasLastTouchPoint) {
    constexpr int16_t kMaxStepPx = 60;
    if (abs(mappedPoint.x - lastTouchPoint.x) > kMaxStepPx ||
        abs(mappedPoint.y - lastTouchPoint.y) > kMaxStepPx) {
      stableTouchSamples = 0;
      touchSessionActive = false;
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
  }

  // Require two consistent reads before emitting PRESSED to suppress idle noise.
  if (stableTouchSamples < 2) {
    stableTouchSamples++;
    lastTouchPoint = mappedPoint;
    hasLastTouchPoint = true;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  if (!touchSessionActive) {
    resetIdleTimer();
    touchSessionActive = true;
  }

  if (now - lastTouchLogMs >= 150) {
    Serial.printf("[TOUCH] raw=(%d,%d,%d) mapped=(%d,%d)\n", rawPoint.x, rawPoint.y, rawPoint.z,
                  mappedPoint.x, mappedPoint.y);
    lastTouchLogMs = now;
  }

  data->state = LV_INDEV_STATE_PRESSED;
  data->point = mappedPoint;
  lastTouchPoint = mappedPoint;
  hasLastTouchPoint = true;
}

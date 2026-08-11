#include <HalGPIO.h>

#include <Logging.h>
#include <esp_sleep.h>

// Global HalGPIO instance
HalGPIO gpio;

namespace {
constexpr uint16_t TOUCH_SWIPE_THRESHOLD = 25;
constexpr unsigned long TOUCH_RELEASE_GRACE_MS = 300;
constexpr unsigned long TOUCH_HOME_BUTTON_DEBOUNCE_MS = 40;
constexpr uint64_t POWER_WAKE_MASK = 1ULL << BoardPins::PowerButton;
constexpr uint64_t TOUCH_WAKE_MASK = 1ULL << BoardPins::TouchInterrupt;

uint8_t buttonBit(uint8_t button) { return static_cast<uint8_t>(1U << button); }

void rotatePhysicalTouchToLogical(uint16_t* x, uint16_t* y) {
#if defined(BOARD_LILYGO_EPD47_S3)
  const uint16_t physicalX = *x;
  const uint16_t physicalY = *y;
  *x = physicalY < BoardPins::LogicalWidth ? BoardPins::LogicalWidth - 1 - physicalY : 0;
  *y = physicalX < BoardPins::LogicalHeight ? physicalX : BoardPins::LogicalHeight - 1;
#else
  (void)x;
  (void)y;
#endif
}
}  // namespace

void HalGPIO::begin() {
  Board::begin();
  const bool touchReady = touch.begin();
  LOG_INF("HW", "Board init: id=%s pca9535=%d touch=%d usb=%d", Board::id(), Board::pca9535Present(), touchReady,
          Board::isUsbConnected());

  lastUsbConnected = isUsbConnected();
  update();
}

void HalGPIO::readTouchState() {
  Board::TouchPoint point;
  bool touchHomeButtonPressed = false;
  const bool havePoint = touch.readPoint(&point, &touchHomeButtonPressed);

  // Edge-detect the touch home button: emit a single event on each press (the transition to
  // pressed) rather than auto-repeating while the finger is held. This lets a quick second tap
  // register instead of being swallowed by a rate-limit window, so fast double-taps work. A short
  // debounce guards against contact bounce on the press edge.
  if (touchHomeButtonPressed) {
    if (!touchHomeButtonHeld && millis() - lastTouchHomeButtonEventTime >= TOUCH_HOME_BUTTON_DEBOUNCE_MS) {
      touchHomeButtonEvent = true;
      lastTouchHomeButtonEventTime = millis();
    }
    touchHomeButtonHeld = true;
  } else {
    touchHomeButtonHeld = false;
  }

  if (!havePoint) {
    if (touchActive && millis() - lastTouchSeenTime > TOUCH_RELEASE_GRACE_MS) {
      if (!touchMoved) {
        touchTapPoint = {touchStartX, touchStartY};
        touchTapEvent = true;
      } else {
        // A moved touch is a swipe/drag. Latch its start and end so higher layers
        // can detect directional gestures (e.g. a top-edge drag-down).
        touchSwipeStart = {touchStartX, touchStartY};
        touchSwipeEnd = currentTouchPoint;
        touchSwipeEvent = true;
      }
      touchActive = false;
    }
    return;
  }

  uint16_t x = point.x;
  uint16_t y = point.y;
  rotatePhysicalTouchToLogical(&x, &y);
  lastTouchSeenTime = millis();

  if (!touchActive) {
    touchActive = true;
    touchStartX = x;
    touchStartY = y;
    currentTouchPoint = {x, y};
    touchStartTime = millis();
    touchMoved = false;
    LOG_DBG("HW", "Touch raw=(%u,%u) logical=(%u,%u)", point.x, point.y, x, y);
  } else {
    currentTouchPoint = {x, y};
    const int dx = static_cast<int>(x) - static_cast<int>(touchStartX);
    const int dy = static_cast<int>(y) - static_cast<int>(touchStartY);
    if (abs(dx) >= TOUCH_SWIPE_THRESHOLD || abs(dy) >= TOUCH_SWIPE_THRESHOLD) {
      touchMoved = true;
    }
  }
}

uint8_t HalGPIO::getState() {
  uint8_t state = 0;

  if (Board::readButton()) {
    state |= buttonBit(BTN_PCA);
  }
  if (digitalRead(BoardPins::PowerButton) == LOW) {
    state |= buttonBit(BTN_POWER);
  }

  readTouchState();
  return state;
}

void HalGPIO::update() {
  const unsigned long currentTime = millis();
  touchTapEvent = false;
  touchSwipeEvent = false;
  touchHomeButtonEvent = false;
  const uint8_t state = getState();

  pressedEvents = 0;
  releasedEvents = 0;

  if (state != lastState) {
    lastDebounceTime = currentTime;
    lastState = state;
  }

  if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (state != currentState) {
      pressedEvents = state & ~currentState;
      releasedEvents = currentState & ~state;

      if (pressedEvents > 0 && currentState == 0) {
        buttonPressStart = currentTime;
      }
      if (releasedEvents > 0 && state == 0) {
        buttonPressFinish = currentTime;
      }

      currentState = state;
    }
  }

  const bool connected = isUsbConnected();
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return currentState & buttonBit(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return pressedEvents & buttonBit(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return pressedEvents > 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return releasedEvents & buttonBit(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return releasedEvents > 0; }

bool HalGPIO::hadTouchActivity() const { return touchActive || touchTapEvent || touchHomeButtonEvent; }

bool HalGPIO::getTouchTap(TouchPoint& point) const {
  if (!touchTapEvent) {
    return false;
  }
  point = touchTapPoint;
  return true;
}

bool HalGPIO::getTouchHold(TouchPoint& point, unsigned long& heldMs) const {
  if (!touchActive || touchMoved) {
    return false;
  }

  point = currentTouchPoint;
  heldMs = millis() - touchStartTime;
  return true;
}

bool HalGPIO::getTouchSwipe(TouchPoint& start, TouchPoint& end) const {
  if (!touchSwipeEvent) {
    return false;
  }
  start = touchSwipeStart;
  end = touchSwipeEnd;
  return true;
}

bool HalGPIO::wasTouchHomeButtonPressed() const { return touchHomeButtonEvent; }

unsigned long HalGPIO::getHeldTime() const {
  if (currentState > 0) {
    return millis() - buttonPressStart;
  }
  return buttonPressFinish - buttonPressStart;
}

void HalGPIO::startDeepSleep(bool wakeOnTouch) {
  while (isPressed(BTN_POWER)) {
    delay(50);
    update();
  }

  Board::deinitForSleep();
  pinMode(BoardPins::PowerButton, INPUT_PULLUP);
  pinMode(BoardPins::TouchInterrupt, INPUT_PULLUP);
  const bool enableTouchWake = wakeOnTouch && Board::capabilities().hasTouchWake;
  const uint64_t wakeMask = enableTouchWake ? (POWER_WAKE_MASK | TOUCH_WAKE_MASK) : POWER_WAKE_MASK;
  LOG_DBG("GPIO", "Entering deep sleep, wakeOnTouch=%d", wakeOnTouch ? 1 : 0);
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
  esp_deep_sleep_start();
}

void HalGPIO::verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed) {
  if (shortPressAllowed) {
    return;
  }

  const uint16_t calibration = millis();
  const uint16_t calibratedDuration = calibration < requiredDurationMs ? requiredDurationMs - calibration : 1;

  const auto start = millis();
  update();
  while (!isPressed(BTN_POWER) && millis() - start < 1000) {
    delay(10);
    update();
  }

  if (isPressed(BTN_POWER)) {
    do {
      delay(10);
      update();
    } while (isPressed(BTN_POWER) && getHeldTime() < calibratedDuration);
    if (getHeldTime() < calibratedDuration) {
      startDeepSleep();
    }
  } else {
    startDeepSleep();
  }
}

bool HalGPIO::isUsbConnected() const { return Board::isUsbConnected(); }

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();
  const bool usbConnected = isUsbConnected();

  if (wakeupCause == ESP_SLEEP_WAKEUP_EXT1) {
    const uint64_t wakeStatus = esp_sleep_get_ext1_wakeup_status();
    if ((wakeStatus & POWER_WAKE_MASK) != 0) {
      return WakeupReason::PowerButton;
    }
    if ((wakeStatus & TOUCH_WAKE_MASK) != 0) {
      return WakeupReason::Touch;
    }
  }

  if (wakeupCause == ESP_SLEEP_WAKEUP_GPIO) {
    if (digitalRead(BoardPins::PowerButton) == LOW) {
      return WakeupReason::PowerButton;
    }
    if (digitalRead(BoardPins::TouchInterrupt) == LOW) {
      return WakeupReason::Touch;
    }
    return WakeupReason::Other;
  }

  if (resetReason == ESP_RST_POWERON && !usbConnected) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}

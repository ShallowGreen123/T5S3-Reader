#pragma once

#include <Arduino.h>
#include <BoardT5S3.h>

class HalGPIO {
 public:
  struct TouchPoint {
    uint16_t x = 0;
    uint16_t y = 0;
  };

 private:
  BoardT5S3::GT911Touch touch;

  uint8_t currentState = 0;
  uint8_t lastState = 0;
  uint8_t pressedEvents = 0;
  uint8_t releasedEvents = 0;
  unsigned long lastDebounceTime = 0;
  unsigned long buttonPressStart = 0;
  unsigned long buttonPressFinish = 0;

  bool touchActive = false;
  uint16_t touchStartX = 0;
  uint16_t touchStartY = 0;
  unsigned long touchStartTime = 0;
  TouchPoint currentTouchPoint;
  unsigned long lastTouchSeenTime = 0;
  bool touchMoved = false;
  bool touchTapEvent = false;
  TouchPoint touchTapPoint;
  bool touchSwipeEvent = false;
  TouchPoint touchSwipeStart;
  TouchPoint touchSwipeEnd;
  bool touchHomeButtonEvent = false;
  bool touchHomeButtonHeld = false;
  unsigned long lastTouchHomeButtonEventTime = 0;

  bool lastUsbConnected = false;
  bool usbStateChanged = false;

  uint8_t getState();
  void readTouchState();

 public:
  enum class DeviceType : uint8_t { T5S3 };

  HalGPIO() = default;

  inline bool deviceIsX3() const { return false; }
  inline bool deviceIsX4() const { return false; }
  inline bool deviceIsT5S3() const { return true; }
  inline const char* getDeviceName() const { return "T5S3"; }

  void begin();
  bool isTouchAvailable() const { return touch.isAvailable(); }

  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  bool hadTouchActivity() const;
  unsigned long getHeldTime() const;
  bool getTouchTap(TouchPoint& point) const;
  bool getTouchHold(TouchPoint& point, unsigned long& heldMs) const;
  // Reports the start and end points of the most recent swipe/drag (a moved touch), in
  // raw portrait-native coordinates. One-shot per release.
  bool getTouchSwipe(TouchPoint& start, TouchPoint& end) const;
  bool wasTouchHomeButtonPressed() const;

  void startDeepSleep(bool wakeOnTouch = true);
  void verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed);

  bool isUsbConnected() const;
  bool wasUsbStateChanged() const;

  enum class WakeupReason { PowerButton, Touch, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
  static constexpr uint8_t BTN_PCA = 7;

 private:
  static constexpr unsigned long DEBOUNCE_DELAY = 5;
};

extern HalGPIO gpio;

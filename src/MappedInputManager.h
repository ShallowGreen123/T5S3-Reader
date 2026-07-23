#pragma once

#include <HalGPIO.h>

class GfxRenderer;

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  struct TouchPoint {
    int16_t x;
    int16_t y;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update() const { gpio.update(); }
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  bool wasTouchTapped(TouchPoint& point, const GfxRenderer& renderer) const;
  bool getTouchHold(TouchPoint& point, unsigned long& heldMs, const GfxRenderer& renderer) const;
  // Reports the start/end of the most recent swipe in oriented (flip-corrected) logical coordinates.
  bool getTouchSwipe(TouchPoint& start, TouchPoint& end, const GfxRenderer& renderer) const;
  bool wasTouchHomeButtonPressed() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  bool resolveTouchFrontButton(size_t slotIndex, Button& button) const;
  void injectButtonTap(Button button);
  void clearInjectedButtonTap();
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

 private:
  HalGPIO& gpio;
  bool hasInjectedButtonTap = false;
  Button injectedButtonTap = Button::Back;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
};

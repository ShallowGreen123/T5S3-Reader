#include "MappedInputManager.h"

#include "CrossPointSettings.h"
#include "GfxRenderer.h"

namespace {
using ButtonIndex = uint8_t;

struct SideLayoutMap {
  ButtonIndex pageBack;
  ButtonIndex pageForward;
};

// Order matches CrossPointSettings::SIDE_BUTTON_LAYOUT.
constexpr SideLayoutMap kSideLayouts[] = {
    {HalGPIO::BTN_UP, HalGPIO::BTN_DOWN},
    {HalGPIO::BTN_DOWN, HalGPIO::BTN_UP},
};

MappedInputManager::TouchPoint orientTouchPoint(const HalGPIO::TouchPoint& raw, const GfxRenderer& renderer) {
  MappedInputManager::TouchPoint point{};

  switch (renderer.getOrientation()) {
    case GfxRenderer::Orientation::Portrait:
      point = {static_cast<int16_t>(raw.x), static_cast<int16_t>(raw.y)};
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      point = {static_cast<int16_t>(renderer.getScreenWidth() - 1 - raw.y), static_cast<int16_t>(raw.x)};
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      point = {static_cast<int16_t>(renderer.getScreenWidth() - 1 - raw.x),
               static_cast<int16_t>(renderer.getScreenHeight() - 1 - raw.y)};
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      point = {static_cast<int16_t>(raw.y), static_cast<int16_t>(renderer.getScreenHeight() - 1 - raw.x)};
      break;
  }

  if (point.x < 0) point.x = 0;
  if (point.y < 0) point.y = 0;
  if (point.x >= renderer.getScreenWidth()) point.x = renderer.getScreenWidth() - 1;
  if (point.y >= renderer.getScreenHeight()) point.y = renderer.getScreenHeight() - 1;

  // When the UI is flipped 180° (mirrored at the display layer), apply the matching
  // logical 180° flip to the touch point. A 180° flip is orientation-independent.
  if (SETTINGS.flipUi) {
    point.x = static_cast<int16_t>(renderer.getScreenWidth() - 1 - point.x);
    point.y = static_cast<int16_t>(renderer.getScreenHeight() - 1 - point.y);
  }
  return point;
}
}  // namespace

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto sideLayout = static_cast<CrossPointSettings::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  const auto& side = kSideLayouts[sideLayout];

  switch (button) {
    case Button::Back:
      // Logical Back maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonBack);
    case Button::Confirm:
      // Logical Confirm maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonConfirm);
    case Button::Left:
      // Logical Left maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonLeft);
    case Button::Right:
      // Logical Right maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonRight);
    case Button::Up:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      return (gpio.*fn)(side.pageBack);
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      return (gpio.*fn)(side.pageForward);
  }

  return false;
}

bool MappedInputManager::wasPressed(const Button button) const {
  return mapButton(button, &HalGPIO::wasPressed) || (hasInjectedButtonTap && injectedButtonTap == button);
}

bool MappedInputManager::wasReleased(const Button button) const {
  return mapButton(button, &HalGPIO::wasReleased) || (hasInjectedButtonTap && injectedButtonTap == button);
}

bool MappedInputManager::isPressed(const Button button) const {
  if (hasInjectedButtonTap && injectedButtonTap == button) {
    return false;
  }
  return mapButton(button, &HalGPIO::isPressed);
}

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed() || hasInjectedButtonTap; }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased() || hasInjectedButtonTap; }

unsigned long MappedInputManager::getHeldTime() const { return hasInjectedButtonTap ? 0 : gpio.getHeldTime(); }

bool MappedInputManager::wasTouchTapped(TouchPoint& point, const GfxRenderer& renderer) const {
  HalGPIO::TouchPoint raw;
  if (!gpio.getTouchTap(raw)) {
    return false;
  }

  point = orientTouchPoint(raw, renderer);
  return true;
}

bool MappedInputManager::getTouchHold(TouchPoint& point, unsigned long& heldMs, const GfxRenderer& renderer) const {
  HalGPIO::TouchPoint raw;
  if (!gpio.getTouchHold(raw, heldMs)) {
    return false;
  }

  point = orientTouchPoint(raw, renderer);
  return true;
}

bool MappedInputManager::wasTouchHomeButtonPressed() const { return gpio.wasTouchHomeButtonPressed(); }

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  // Build the label order based on the configured hardware mapping.
  auto labelForHardware = [&](uint8_t hw) -> const char* {
    // Compare against configured logical roles and return the matching label.
    if (hw == SETTINGS.frontButtonBack) {
      return back;
    }
    if (hw == SETTINGS.frontButtonConfirm) {
      return confirm;
    }
    if (hw == SETTINGS.frontButtonLeft) {
      return previous;
    }
    if (hw == SETTINGS.frontButtonRight) {
      return next;
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

bool MappedInputManager::resolveTouchFrontButton(const size_t slotIndex, Button& button) const {
  constexpr uint8_t kFrontButtons[] = {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT};
  if (slotIndex >= sizeof(kFrontButtons)) {
    return false;
  }

  const uint8_t hardwareButton = kFrontButtons[slotIndex];
  if (hardwareButton == SETTINGS.frontButtonBack) {
    button = Button::Back;
    return true;
  }
  if (hardwareButton == SETTINGS.frontButtonConfirm) {
    button = Button::Confirm;
    return true;
  }
  if (hardwareButton == SETTINGS.frontButtonLeft) {
    button = Button::Left;
    return true;
  }
  if (hardwareButton == SETTINGS.frontButtonRight) {
    button = Button::Right;
    return true;
  }
  return false;
}

void MappedInputManager::injectButtonTap(const Button button) {
  injectedButtonTap = button;
  hasInjectedButtonTap = true;
}

void MappedInputManager::clearInjectedButtonTap() { hasInjectedButtonTap = false; }

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}

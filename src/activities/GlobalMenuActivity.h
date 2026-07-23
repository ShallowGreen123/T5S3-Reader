#pragma once
#include <string>

#include "Activity.h"
#include "util/ButtonNavigator.h"

// A lightweight top-aligned popup menu that can be opened from any activity (via a
// top-edge drag-down gesture or an optional double-click of the home button). It renders
// as an overlay on top of whatever was on screen (it does not clear the framebuffer) and
// dismisses when the user taps below the panel, returning them to what they were doing.
//
// Currently hosts the Backlight and Shut Down controls.
class GlobalMenuActivity final : public Activity {
 public:
  // overGrayscaleReader must be set when the menu opens over a reader page, which
  // leaves the e-ink panel in a 4-level grayscale state. A partial waveform cannot
  // cleanly transition grayscale particles to the menu's 1-bit BW content, so the
  // first paint uses a FULL_REFRESH in that case.
  GlobalMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool overGrayscaleReader = false);

  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;

  // A single home-button press dismisses the menu (rather than navigating home).
  bool onTouchHomeButton() override {
    finish();
    return true;
  }

  // Don't allow the global menu to open over itself.
  bool supportsGlobalMenu() const override { return false; }

 private:
  enum { BUTTON_BACKLIGHT = 0, BUTTON_SHUTDOWN = 1, BUTTON_COUNT = 2 };

  void getPanelLayout(int& panelX, int& panelY, int& panelW, int& panelH) const;
  void getButtonRect(int index, int& x, int& y, int& w, int& h) const;
  void drawButtonBox(int x, int y, int width, int height, bool focused);
  void drawBacklightButton(int x, int y, int width, int height, bool focused, int level);
  void drawActionButton(int x, int y, int width, int height, bool focused, const std::string& label);
  void applyBacklightLevel(int level);
  void activateSelection();
  void triggerShutdown();

  int selectedIndex = BUTTON_BACKLIGHT;
  bool overGrayscaleReader = false;
  ButtonNavigator buttonNavigator;
};

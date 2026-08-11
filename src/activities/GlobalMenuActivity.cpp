#include "GlobalMenuActivity.h"

#include <Board.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "PowerControl.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kPanelMargin = 10;   // gap from the screen edges
constexpr int kPanelTopGap = 8;    // gap from the very top edge
constexpr int kInnerPad = 16;      // padding inside the panel
constexpr int kButtonHeight = 60;
constexpr int kButtonGap = 14;
constexpr int kCornerRadius = 12;
constexpr int kArrowRegionHeight = 44;  // band below the panel that holds the "hide" up-arrow
constexpr int kArrowHalfWidth = 20;
constexpr int kArrowHeight = 18;

bool hasBacklight() { return Board::capabilities().hasBacklight; }
}  // namespace

GlobalMenuActivity::GlobalMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       bool overGrayscaleReader)
    : Activity("GlobalMenu", renderer, mappedInput), overGrayscaleReader(overGrayscaleReader) {}

void GlobalMenuActivity::onEnter() {
  Activity::onEnter();
  if (!hasBacklight()) {
    selectedIndex = BUTTON_SHUTDOWN;
  }
  requestUpdate();
}

void GlobalMenuActivity::getPanelLayout(int& panelX, int& panelY, int& panelW, int& panelH) const {
  const int screenW = renderer.getScreenWidth();
  panelX = kPanelMargin;
  panelY = kPanelTopGap;
  panelW = screenW - kPanelMargin * 2;
  const int visibleButtonCount = hasBacklight() ? BUTTON_COUNT : 1;
  panelH = kInnerPad * 2 + visibleButtonCount * kButtonHeight + (visibleButtonCount - 1) * kButtonGap;
}

void GlobalMenuActivity::getButtonRect(int index, int& x, int& y, int& w, int& h) const {
  int panelX, panelY, panelW, panelH;
  getPanelLayout(panelX, panelY, panelW, panelH);
  x = panelX + kInnerPad;
  w = panelW - kInnerPad * 2;
  h = kButtonHeight;
  const int row = hasBacklight() ? index : 0;
  y = panelY + kInnerPad + row * (kButtonHeight + kButtonGap);
}

void GlobalMenuActivity::loop() {
  const auto moveNext = [this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, BUTTON_COUNT);
    requestUpdate();
  };
  const auto movePrevious = [this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, BUTTON_COUNT);
    requestUpdate();
  };

  if (!hasBacklight()) {
    selectedIndex = BUTTON_SHUTDOWN;
  } else if (selectedIndex == BUTTON_BACKLIGHT) {
    // While the backlight button is focused, Left/Right adjust brightness and only
    // Up/Down navigate, so the two functions don't collide on the same buttons.
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right},
                                         [this] { applyBacklightLevel(SETTINGS.backlightLevel + 1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left},
                                         [this] { applyBacklightLevel(SETTINGS.backlightLevel - 1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, moveNext);
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, movePrevious);
  } else {
    buttonNavigator.onNext(moveNext);
    buttonNavigator.onPrevious(movePrevious);
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
    return;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();  // dismiss
    return;
  }
}

bool GlobalMenuActivity::onTouchTap(int16_t x, int16_t y) {
  int panelX, panelY, panelW, panelH;
  getPanelLayout(panelX, panelY, panelW, panelH);

  // A tap outside the panel (typically below it) dismisses the menu and returns the
  // user to what they were doing.
  const bool insidePanel = x >= panelX && x < panelX + panelW && y >= panelY && y < panelY + panelH;
  if (!insidePanel) {
    finish();
    return true;
  }

  // Backlight button: left half decreases, right half increases (the -/+ glyphs are affordances).
  int bx, by, bw, bh;
  if (hasBacklight()) {
    getButtonRect(BUTTON_BACKLIGHT, bx, by, bw, bh);
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
      selectedIndex = BUTTON_BACKLIGHT;
      applyBacklightLevel(x < bx + bw / 2 ? SETTINGS.backlightLevel - 1 : SETTINGS.backlightLevel + 1);
      requestUpdate();
      return true;
    }
  }

  getButtonRect(BUTTON_SHUTDOWN, bx, by, bw, bh);
  if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
    selectedIndex = BUTTON_SHUTDOWN;
    triggerShutdown();
    return true;
  }

  // Tap on the panel background (not a button): keep the menu open.
  return true;
}

void GlobalMenuActivity::activateSelection() {
  if (selectedIndex == BUTTON_BACKLIGHT) {
    // Confirm cycles the shared global backlight level up, wrapping 10 -> 0.
    applyBacklightLevel(SETTINGS.backlightLevel >= 10 ? 0 : SETTINGS.backlightLevel + 1);
    return;
  }
  triggerShutdown();
}

void GlobalMenuActivity::applyBacklightLevel(int level) {
  if (!hasBacklight()) {
    return;
  }
  if (level < 0) level = 0;
  if (level > 10) level = 10;
  if (level == SETTINGS.backlightLevel) {
    return;
  }
  SETTINGS.backlightLevel = static_cast<uint8_t>(level);
  Board::setBacklightLevel(SETTINGS.backlightLevel);
  SETTINGS.saveToFile();
  requestUpdate();
}

void GlobalMenuActivity::triggerShutdown() {
  if (SETTINGS.confirmShutdown) {
    startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, I18N.get(StrId::STR_SHUTDOWN),
                                                                  I18N.get(StrId::STR_SHUTDOWN_PROMPT)),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               requestShutdown();
                             }
                             // Dismiss the menu either way: on cancel this restores the
                             // underlying screen; on confirm the device powers off shortly.
                             finish();
                           });
    return;
  }
  requestShutdown();
}

void GlobalMenuActivity::drawButtonBox(int x, int y, int width, int height, bool focused) {
  if (focused) {
    renderer.fillRoundedRect(x, y, width, height, kCornerRadius, Color::Black);
  } else {
    renderer.drawRoundedRect(x, y, width, height, 2, kCornerRadius, true);
  }
}

void GlobalMenuActivity::drawBacklightButton(int x, int y, int width, int height, bool focused, int level) {
  drawButtonBox(x, y, width, height, focused);
  const bool textBlack = !focused;

  const std::string centerText = std::string(I18N.get(StrId::STR_BACKLIGHT)) + "   " + std::to_string(level);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textY = y + (height - lineHeight) / 2;
  const int centerWidth = renderer.getTextWidth(UI_10_FONT_ID, centerText.c_str());
  renderer.drawText(UI_10_FONT_ID, x + (width - centerWidth) / 2, textY, centerText.c_str(), textBlack);

  const int glyphLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int glyphY = y + (height - glyphLineHeight) / 2;
  constexpr int glyphPadding = 26;
  renderer.drawText(UI_12_FONT_ID, x + glyphPadding, glyphY, "-", textBlack, EpdFontFamily::BOLD);
  const int plusWidth = renderer.getTextWidth(UI_12_FONT_ID, "+", EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, x + width - glyphPadding - plusWidth, glyphY, "+", textBlack, EpdFontFamily::BOLD);
}

void GlobalMenuActivity::drawActionButton(int x, int y, int width, int height, bool focused,
                                          const std::string& label) {
  drawButtonBox(x, y, width, height, focused);
  const bool textBlack = !focused;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textY = y + (height - lineHeight) / 2;
  const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
  renderer.drawText(UI_10_FONT_ID, x + (width - labelWidth) / 2, textY, label.c_str(), textBlack);
}

void GlobalMenuActivity::render(RenderLock&&) {
  // Top-only overlay: paint an opaque white band over just the top of the screen for
  // the panel, and leave everything below untouched so the previous content (e.g. the
  // reader page still in the shared framebuffer) shows through. An up-arrow under the
  // Shut Down button is the affordance to hide the menu.
  int panelX, panelY, panelW, panelH;
  getPanelLayout(panelX, panelY, panelW, panelH);
  const int screenW = renderer.getScreenWidth();
  const int panelBottom = panelY + panelH;
  const int dividerY = panelBottom + kArrowRegionHeight;

  // White background over the top band only (false = white); bottom content preserved.
  renderer.fillRect(0, 0, screenW, dividerY, false);

  int bx, by, bw, bh;
  if (hasBacklight()) {
    getButtonRect(BUTTON_BACKLIGHT, bx, by, bw, bh);
    drawBacklightButton(bx, by, bw, bh, selectedIndex == BUTTON_BACKLIGHT, SETTINGS.backlightLevel);
  }

  getButtonRect(BUTTON_SHUTDOWN, bx, by, bw, bh);
  drawActionButton(bx, by, bw, bh, selectedIndex == BUTTON_SHUTDOWN, I18N.get(StrId::STR_SHUTDOWN));

  // Up-arrow centered under the Shut Down button (full-width button, so screen center).
  // Apex on top, base below -> points up to signal "hide / collapse the menu".
  const int arrowCenterX = screenW / 2;
  const int arrowTopY = panelBottom + (kArrowRegionHeight - kArrowHeight) / 2;
  const int arrowXPoints[3] = {arrowCenterX, arrowCenterX - kArrowHalfWidth, arrowCenterX + kArrowHalfWidth};
  const int arrowYPoints[3] = {arrowTopY, arrowTopY + kArrowHeight, arrowTopY + kArrowHeight};
  renderer.fillPolygon(arrowXPoints, arrowYPoints, 3, true);

  // Thin divider separating the menu band from the preserved content below.
  renderer.drawLine(0, dividerY, screenW, dividerY, true);

  // Over a reader the panel is in a grayscale state; only a full refresh transitions
  // it cleanly to the menu's 1-bit BW content and keeps the preserved page below crisp.
  // Elsewhere FAST_REFRESH is overridden to HALF_REFRESH by the push transition, so this
  // preserves the existing (working) behavior for non-reader screens.
  renderer.displayBuffer(overGrayscaleReader ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
}

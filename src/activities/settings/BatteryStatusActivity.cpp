#include "BatteryStatusActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int CARD_RADIUS = 12;
constexpr int CARD_PADDING = 12;
constexpr int CARD_GAP = 10;

const char* yesNo(bool value) { return value ? "Yes" : "No"; }

const char* onlineText(bool ready, bool readOk) {
  if (!ready) {
    return "Not found";
  }
  return readOk ? "Online" : "Read error";
}

const char* chargeStatusName(Board::BatteryChargeStatus status) {
  switch (status) {
    case Board::BatteryChargeStatus::NotCharging:
      return "Not charging";
    case Board::BatteryChargeStatus::Precharge:
      return "Precharge";
    case Board::BatteryChargeStatus::FastCharge:
      return "Fast charge";
    case Board::BatteryChargeStatus::Done:
      return "Done";
    case Board::BatteryChargeStatus::Unknown:
    default:
      return "Unknown";
  }
}

const char* gaugeStateName(Board::BatteryGaugeState state) {
  switch (state) {
    case Board::BatteryGaugeState::Sleep:
      return "Sleep";
    case Board::BatteryGaugeState::Full:
      return "Full";
    case Board::BatteryGaugeState::Charge:
      return "Charge";
    case Board::BatteryGaugeState::Discharge:
      return "Discharge";
    case Board::BatteryGaugeState::Relax:
      return "Relax";
    case Board::BatteryGaugeState::Unknown:
    default:
      return "Unknown";
  }
}

std::string formatTemperature(uint16_t deciKelvin) {
  if (deciKelvin == 0) {
    return "--";
  }

  const int deciCelsius = static_cast<int>(deciKelvin) - 2731;
  char text[24];
  std::snprintf(text, sizeof(text), "%d.%d C", deciCelsius / 10, std::abs(deciCelsius % 10));
  return text;
}

std::string primaryModeText(const Board::BatteryState& state, bool hasState) {
  if (!hasState) {
    return "Unavailable";
  }

  if (state.gaugeReady && state.gaugeReadOk) {
    return gaugeStateName(state.gaugeState);
  }

  if (state.chargeDone) {
    return "Full";
  }

  if (state.charging) {
    return "Charging";
  }

  if (state.chargerReady || state.gaugeReady) {
    return state.vbusConnected ? "Standby" : "Discharge";
  }

  return "Unavailable";
}

std::string summaryText(const Board::BatteryState& state, bool hasState) {
  const auto& profile = Board::batteryProfile();
  if (!hasState) {
    return "Battery management unavailable";
  }

  if (!Board::capabilities().hasDetailedBatteryTelemetry) {
    char basicText[96];
    const uint16_t voltage = state.gaugeReadOk ? state.gaugeVoltageMv : state.batteryVoltageMv;
    std::snprintf(basicText, sizeof(basicText), "%u%% | %u mV | ADC estimate", state.socPercent, voltage);
    return basicText;
  }

  char text[160];
  const uint16_t voltage = state.gaugeReadOk ? state.gaugeVoltageMv : state.batteryVoltageMv;
  if (state.gaugeReadOk) {
    std::snprintf(text,
                  sizeof(text),
                  "%s | %u%% | %u mV | Avg %d mA | Model %u mAh",
                  state.vbusConnected ? "USB IN" : "USB OUT",
                  state.socPercent,
                  voltage,
                  static_cast<int>(state.averageCurrentMa),
                  profile.capacityMah);
  } else {
    std::snprintf(text,
                  sizeof(text),
                  "%s | Gauge %s | Charger %s | Model %u mAh",
                  state.vbusConnected ? "USB IN" : "USB OUT",
                  onlineText(state.gaugeReady, state.gaugeReadOk),
                  onlineText(state.chargerReady, state.chargerReadOk),
                  profile.capacityMah);
  }
  return text;
}

std::string lineText(const char* label, const std::string& value) {
  std::string line(label);
  line += " : ";
  line += value;
  return line;
}

std::string lineText(const char* label, const char* value) { return lineText(label, std::string(value)); }

std::string lineText(const char* label, int value, const char* unit) {
  char text[40];
  std::snprintf(text, sizeof(text), "%d %s", value, unit);
  return lineText(label, text);
}

std::string lineText(const char* label, unsigned value, const char* unit) {
  char text[40];
  std::snprintf(text, sizeof(text), "%u %s", value, unit);
  return lineText(label, text);
}

std::string hexLineText(const char* label, uint16_t value) {
  char text[16];
  std::snprintf(text, sizeof(text), "0x%04X", value);
  return lineText(label, text);
}

std::array<std::string, 11> gaugeLines(const Board::BatteryState& state) {
  if (!state.gaugeReady) {
    return {lineText("VBUS", "--"),
            lineText("State", "Not found"),
            lineText("SOC/FCC/SOH", "--"),
            lineText("Temp", "--"),
            lineText("AvgI", "--"),
            lineText("Volt", "--"),
            lineText("ChgV", "--"),
            lineText("TapI", "--"),
            lineText("Finished", "--"),
            lineText("Rem/Full", "--"),
            lineText("Dbg", "--")};
  }

  if (!state.gaugeReadOk) {
    return {lineText("VBUS", "--"),
            lineText("State", "Read error"),
            lineText("SOC/FCC/SOH", "--"),
            lineText("Temp", "--"),
            lineText("AvgI", "--"),
            lineText("Volt", "--"),
            lineText("ChgV", "--"),
            lineText("TapI", "--"),
            lineText("Finished", "--"),
            lineText("Rem/Full", "--"),
            hexLineText("Status", state.batteryStatusRaw)};
  }

  char combined[72];
  std::snprintf(combined,
                sizeof(combined),
                "%u%%/%u/%u%%",
                state.socPercent,
                state.fullCapacityMah,
                state.sohPercent);

  char remFull[72];
  std::snprintf(remFull, sizeof(remFull), "%u/%u mAh", state.remainingCapacityMah, state.fullCapacityMah);

  char dbg[96];
  std::snprintf(dbg,
                sizeof(dbg),
                "BFC=%u GFC=%u TCA=%u AI=%u",
                state.gaugeBatteryFullFlag ? 1u : 0u,
                state.gaugeGaugingFullFlag ? 1u : 0u,
                state.gaugeTaperFlag ? 1u : 0u,
                state.gaugeChargeInhibit ? 1u : 0u);

  return {lineText("VBUS", state.vbusConnected ? "IN" : "OUT"),
          lineText("State", gaugeStateName(state.gaugeState)),
          lineText("SOC/FCC/SOH", combined),
          lineText("Temp", formatTemperature(state.temperatureDk)),
          lineText("AvgI", static_cast<int>(state.averageCurrentMa), "mA"),
          lineText("Volt", static_cast<unsigned>(state.gaugeVoltageMv), "mV"),
          lineText("ChgV", static_cast<unsigned>(state.gaugeChargeVoltageMv), "mV"),
          lineText("TapI", static_cast<unsigned>(state.gaugeTaperCurrentMa), "mA"),
          lineText("Finished", yesNo(state.chargeDone)),
          lineText("Rem/Full", remFull),
          lineText("Dbg", dbg)};
}

std::array<std::string, 9> chargerLines(const Board::BatteryState& state) {
  if (!state.chargerReady) {
    return {lineText("VBUS", "--"),
            lineText("VBUS mV", "--"),
            lineText("VSYS", "--"),
            lineText("VBAT", "--"),
            lineText("VREG", "--"),
            lineText("ICHG", "--"),
            lineText("PRE/TERM", "--"),
            lineText("CHG ADC", "--"),
            lineText("Status", "Not found")};
  }

  if (!state.chargerReadOk) {
    return {lineText("VBUS", "--"),
            lineText("VBUS mV", "--"),
            lineText("VSYS", "--"),
            lineText("VBAT", "--"),
            lineText("VREG", "--"),
            lineText("ICHG", "--"),
            lineText("PRE/TERM", "--"),
            lineText("CHG ADC", "--"),
            lineText("Status", "Read error")};
  }

  char preTerm[48];
  std::snprintf(preTerm, sizeof(preTerm), "%u/%u mA", state.prechargeCurrentMa, state.terminationCurrentMa);

  char status[72];
  std::snprintf(status,
                sizeof(status),
                "%s / %s",
                chargeStatusName(state.chargerStatus),
                state.chargeEnabled ? "Enabled" : "Disabled");

  return {lineText("VBUS", state.vbusConnected ? "IN" : "OUT"),
          lineText("VBUS mV", static_cast<unsigned>(state.vbusVoltageMv), "mV"),
          lineText("VSYS", static_cast<unsigned>(state.systemVoltageMv), "mV"),
          lineText("VBAT", static_cast<unsigned>(state.batteryVoltageMv), "mV"),
          lineText("VREG", static_cast<unsigned>(state.chargeVoltageMv), "mV"),
          lineText("ICHG", static_cast<unsigned>(state.chargeCurrentMa), "mA"),
          lineText("PRE/TERM", preTerm),
          lineText("CHG ADC", static_cast<unsigned>(state.chargerAdcCurrentMa), "mA"),
          lineText("Status", status)};
}

std::array<std::string, 4> basicBatteryLines(const Board::BatteryState& state) {
  return {lineText("Board", Board::displayName()),
          lineText("State", gaugeStateName(state.gaugeState)),
          lineText("Charge", static_cast<unsigned>(state.socPercent), "%"),
          lineText("Voltage", static_cast<unsigned>(state.gaugeVoltageMv), "mV")};
}

template <size_t N>
void drawPanel(const GfxRenderer& renderer, Rect rect, const char* title, const std::array<std::string, N>& lines) {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 2, CARD_RADIUS, true);
  renderer.drawText(UI_12_FONT_ID, rect.x + CARD_PADDING, rect.y + CARD_PADDING, title, true, EpdFontFamily::BOLD);
  renderer.drawLine(rect.x + CARD_PADDING,
                    rect.y + CARD_PADDING + 28,
                    rect.x + rect.width - CARD_PADDING,
                    rect.y + CARD_PADDING + 28);

  const int lineHeight = std::max(16, renderer.getLineHeight(SMALL_FONT_ID) + 2);
  int y = rect.y + CARD_PADDING + 42;
  const int maxTextWidth = rect.width - CARD_PADDING * 2;
  for (const auto& line : lines) {
    if (y + lineHeight > rect.y + rect.height - CARD_PADDING) {
      break;
    }
    const auto text = renderer.truncatedText(SMALL_FONT_ID, line.c_str(), maxTextWidth);
    renderer.drawText(SMALL_FONT_ID, rect.x + CARD_PADDING, y, text.c_str());
    y += lineHeight;
  }
}

}  // namespace

void BatteryStatusActivity::onEnter() {
  Activity::onEnter();
  refreshBattery();
  requestUpdate();
}

void BatteryStatusActivity::refreshBattery() {
  Board::beginBatteryManagement();
  hasState = Board::readBatteryState(&state);
}

void BatteryStatusActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    refreshBattery();
    requestUpdate();
    return;
  }
}

bool BatteryStatusActivity::onTouchTap(int16_t x, int16_t y) {
  MappedInputManager::Button button = MappedInputManager::Button::Back;
  if (!resolveTouchButtonHint(x, y, button)) {
    return false;
  }

  if (button == MappedInputManager::Button::Back) {
    finish();
    return true;
  }

  if (button == MappedInputManager::Button::Confirm) {
    refreshBattery();
    requestUpdate();
    return true;
  }

  return true;
}

void BatteryStatusActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int sidePadding = metrics.contentSidePadding;

  const auto mode = primaryModeText(state, hasState);
  const auto summary = summaryText(state, hasState);
  renderer.drawText(UI_12_FONT_ID, sidePadding, contentTop, mode.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, sidePadding, contentTop + 30, summary.c_str());

  const int panelsTop = contentTop + 62;
  const int panelsHeight = std::max(0, contentBottom - panelsTop);
  if (!Board::capabilities().hasDetailedBatteryTelemetry) {
    drawPanel(renderer,
              Rect{sidePadding, panelsTop, pageWidth - sidePadding * 2, panelsHeight},
              "Battery ADC",
              basicBatteryLines(state));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_UPDATE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const bool sideBySide = pageWidth >= 760 && pageWidth >= pageHeight;
  if (sideBySide) {
    const int panelWidth = (pageWidth - sidePadding * 2 - CARD_GAP) / 2;
    drawPanel(renderer, Rect{sidePadding, panelsTop, panelWidth, panelsHeight}, "BQ27220", gaugeLines(state));
    drawPanel(renderer,
              Rect{sidePadding + panelWidth + CARD_GAP, panelsTop, panelWidth, panelsHeight},
              "BQ25896",
              chargerLines(state));
  } else {
    const int panelHeight = (panelsHeight - CARD_GAP) / 2;
    drawPanel(renderer, Rect{sidePadding, panelsTop, pageWidth - sidePadding * 2, panelHeight}, "BQ27220",
              gaugeLines(state));
    drawPanel(renderer,
              Rect{sidePadding, panelsTop + panelHeight + CARD_GAP, pageWidth - sidePadding * 2, panelHeight},
              "BQ25896",
              chargerLines(state));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_UPDATE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#include "TimeZoneSelectActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

void TimeZoneSelectActivity::onEnter() {
  Activity::onEnter();
  region_ = TimeZoneCatalog::regionOf(SETTINGS.timeZoneId);
  mode_ = Mode::Region;
  selectedIndex = static_cast<int>(region_);
  requestUpdate();
}

void TimeZoneSelectActivity::onExit() { Activity::onExit(); }

int TimeZoneSelectActivity::currentItemCount() const {
  if (mode_ == Mode::Region) {
    return static_cast<int>(TimeZoneRegion::Count);
  }
  return static_cast<int>(regionCityCount_);
}

const char* TimeZoneSelectActivity::regionTitle(const TimeZoneRegion region) const {
  return TimeZoneCatalog::regionDisplayName(region);
}

void TimeZoneSelectActivity::loadRegionCities(const TimeZoneRegion region) {
  regionCityCount_ = 0;
  const auto* entries = TimeZoneCatalog::data();
  const uint16_t n = TimeZoneCatalog::count();
  for (uint16_t i = 0; i < n && regionCityCount_ < TimeZoneCatalog::kMaxCitiesPerRegion; ++i) {
    if (entries[i].region == region) {
      regionCityIndices_[regionCityCount_++] = i;
    }
  }
}

void TimeZoneSelectActivity::applySelection(const char* id) {
  TimeZoneCatalog::copyId(SETTINGS.timeZoneId, sizeof(SETTINGS.timeZoneId), id);
  halClock.configure(SETTINGS.timeZoneId, SETTINGS.rtcStoresUtc != 0, SETTINGS.rtcVariantHint,
                     SETTINGS.rtcReferenceEpoch);
  (void)halClock.syncSystemTimeFromRtc();
  SETTINGS.rtcStoresUtc = halClock.getRtcStoresUtc() ? 1 : 0;
  SETTINGS.saveToFile();
  finish();
}

void TimeZoneSelectActivity::onBack() {
  if (mode_ == Mode::City) {
    mode_ = Mode::Region;
    selectedIndex = static_cast<int>(region_);
    requestUpdate();
    return;
  }
  finish();
}

void TimeZoneSelectActivity::handleSelection() {
  if (mode_ == Mode::Region) {
    region_ = static_cast<TimeZoneRegion>(selectedIndex);
    loadRegionCities(region_);
    mode_ = Mode::City;
    selectedIndex = 0;
    const int16_t current = TimeZoneCatalog::indexOf(SETTINGS.timeZoneId);
    for (uint16_t i = 0; i < regionCityCount_; ++i) {
      if (regionCityIndices_[i] == static_cast<uint16_t>(current)) {
        selectedIndex = static_cast<int>(i);
        break;
      }
    }
    requestUpdate();
    return;
  }

  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(regionCityCount_)) {
    return;
  }
  applySelection(TimeZoneCatalog::get(regionCityIndices_[static_cast<uint16_t>(selectedIndex)]).id);
}

void TimeZoneSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int itemCount = currentItemCount();
  buttonNavigator.onNextRelease([this, itemCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, itemCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });
}

bool TimeZoneSelectActivity::onTouchTap(int16_t, int16_t y) {
  const auto pageHeight = renderer.getScreenHeight();
  const auto metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int rowHeight = metrics.listRowHeight;
  if (rowHeight <= 0 || y < contentTop || y >= contentTop + contentHeight) {
    return false;
  }

  const int itemCount = currentItemCount();
  const int pageItems = std::max(1, contentHeight / rowHeight);
  const int row = (y - contentTop) / rowHeight;
  const int pageStartIndex = (selectedIndex / pageItems) * pageItems;
  const int touchedIndex = pageStartIndex + row;
  if (row < 0 || row >= pageItems || touchedIndex < 0 || touchedIndex >= itemCount) {
    return false;
  }

  selectedIndex = touchedIndex;
  handleSelection();
  return true;
}

void TimeZoneSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto metrics = UITheme::getInstance().getMetrics();
  const char* title = mode_ == Mode::Region ? tr(STR_TIME_ZONE) : regionTitle(region_);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int itemCount = currentItemCount();
  const int16_t currentZoneIndex = TimeZoneCatalog::indexOf(SETTINGS.timeZoneId);
  const TimeZoneRegion currentRegion = TimeZoneCatalog::regionOf(SETTINGS.timeZoneId);

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
      [this](int index) -> std::string {
        if (mode_ == Mode::Region) {
          return regionTitle(static_cast<TimeZoneRegion>(index));
        }
        char name[TimeZoneCatalog::kMaxIdLength];
        TimeZoneCatalog::formatDisplayName(TimeZoneCatalog::get(regionCityIndices_[static_cast<uint16_t>(index)]).id,
                                           name, sizeof(name));
        return name;
      },
      nullptr, nullptr,
      [this, currentZoneIndex, currentRegion](int index) -> std::string {
        if (mode_ == Mode::Region) {
          return static_cast<TimeZoneRegion>(index) == currentRegion ? tr(STR_SELECTED) : "";
        }
        return regionCityIndices_[static_cast<uint16_t>(index)] == static_cast<uint16_t>(currentZoneIndex)
                   ? tr(STR_SELECTED)
                   : "";
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

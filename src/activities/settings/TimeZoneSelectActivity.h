#pragma once

#include <TimeZoneCatalog.h>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class MappedInputManager;

class TimeZoneSelectActivity final : public Activity {
 public:
  explicit TimeZoneSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TimeZoneSelect", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;

 private:
  enum class Mode : uint8_t { Region, City };

  void loadRegionCities(TimeZoneRegion region);
  void handleSelection();
  void applySelection(const char* id);
  void onBack();
  int currentItemCount() const;
  const char* regionTitle(TimeZoneRegion region) const;

  ButtonNavigator buttonNavigator;
  Mode mode_ = Mode::Region;
  TimeZoneRegion region_ = TimeZoneRegion::UTC;
  uint16_t regionCityIndices_[TimeZoneCatalog::kMaxCitiesPerRegion] = {};
  uint16_t regionCityCount_ = 0;
  int selectedIndex = 0;
};

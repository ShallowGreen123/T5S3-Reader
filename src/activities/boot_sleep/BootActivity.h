#pragma once
#include "../Activity.h"

class BootActivity final : public Activity {
 public:
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Boot", renderer, mappedInput) {}
  void onEnter() override;
  bool supportsTouchHomeButton() const override { return false; }
  bool showsHomeTouchButton() const override { return false; }
  bool supportsGlobalMenu() const override { return false; }
};

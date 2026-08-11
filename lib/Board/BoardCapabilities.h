#pragma once

struct BoardCapabilities {
  bool hasBacklight = false;
  bool hasDetailedBatteryTelemetry = false;
  bool hasHardPowerOff = false;
  bool hasTouchWake = false;
  bool hasTouch = false;
  bool hasRtc = false;
};

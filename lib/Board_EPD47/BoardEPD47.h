#pragma once

#include <Arduino.h>

#include <BoardCapabilities.h>

#include "pin.hpp"

namespace BoardEPD47 {

const char* id();
const char* displayName();
const char* firmwareMarker();
const BoardCapabilities& capabilities();

class ScopedI2CLock {
 public:
  ScopedI2CLock();
  ~ScopedI2CLock();

  ScopedI2CLock(const ScopedI2CLock&) = delete;
  ScopedI2CLock& operator=(const ScopedI2CLock&) = delete;

 private:
  bool locked_ = false;
};

void begin();
void beginI2C();
void initBacklight();
void setBacklightLevel(uint8_t level);
void prepareSdBus();
void disableGpsLora();
void deinitForSleep();

struct BatteryProfile {
  uint16_t inputLimitMa = 0;
  uint16_t capacityMah = 0;
  uint16_t chargeCurrentMa = 0;
  uint16_t prechargeCurrentMa = 0;
  uint16_t terminationCurrentMa = 0;
  uint16_t chargeVoltageMv = 0;
  uint16_t chargeTerminationVoltageDeltaMv = 0;
  uint16_t systemMinVoltageMv = 0;
  int16_t currentThresholdMa = 0;
};

enum class BatteryChargeStatus : uint8_t {
  NotCharging = 0,
  Precharge = 1,
  FastCharge = 2,
  Done = 3,
  Unknown = 0xFF,
};

enum class BatteryGaugeState : uint8_t {
  Sleep = 0,
  Full = 1,
  Charge = 2,
  Discharge = 3,
  Relax = 4,
  Unknown = 0xFF,
};

struct BatteryState {
  bool chargerReady = false;
  bool gaugeReady = false;
  bool chargerReadOk = false;
  bool gaugeReadOk = false;
  bool vbusConnected = false;
  bool chargeEnabled = false;
  bool charging = false;
  bool chargeDone = false;
  bool gaugeBatteryFullFlag = false;
  bool gaugeGaugingFullFlag = false;
  bool gaugeTaperFlag = false;
  bool gaugeChargeInhibit = false;
  uint8_t chargerVbusStatus = 0;
  BatteryChargeStatus chargerStatus = BatteryChargeStatus::Unknown;
  BatteryGaugeState gaugeState = BatteryGaugeState::Unknown;
  uint16_t inputLimitMa = 0;
  uint16_t chargeCurrentMa = 0;
  uint16_t prechargeCurrentMa = 0;
  uint16_t terminationCurrentMa = 0;
  uint16_t chargerAdcCurrentMa = 0;
  uint16_t chargeVoltageMv = 0;
  uint16_t systemVoltageMv = 0;
  uint16_t batteryVoltageMv = 0;
  uint16_t vbusVoltageMv = 0;
  uint16_t gaugeVoltageMv = 0;
  uint16_t gaugeChargeVoltageMv = 0;
  uint16_t gaugeTaperCurrentMa = 0;
  uint16_t socPercent = 0;
  uint16_t sohPercent = 0;
  uint16_t fullCapacityMah = 0;
  uint16_t remainingCapacityMah = 0;
  uint16_t temperatureDk = 0;
  uint16_t batteryStatusRaw = 0;
  uint16_t gaugingStatusRaw = 0;
  int16_t currentMa = 0;
  int16_t averageCurrentMa = 0;
};

const BatteryProfile& batteryProfile();
bool beginBatteryManagement();
bool isBatteryManagementReady();
bool readBatteryState(BatteryState* state);
bool shutdownBatteryPower();
bool pca9535Present();
bool readPca9535Pin(uint8_t pin, bool* high);
bool writePca9535Pin(uint8_t pin, bool high);
bool setPca9535PinMode(uint8_t pin, uint8_t mode);
bool readButton();

bool readBQ27220Reg16(uint8_t reg, uint16_t* value);
bool readBQ25896Reg8(uint8_t reg, uint8_t* value);
bool readBatteryStateOfCharge(uint16_t* soc);
bool readBatteryCurrentMa(int16_t* current);
bool readBatteryAverageCurrentMa(int16_t* current);
bool isUsbConnected();

struct TouchPoint {
  uint16_t x = 0;
  uint16_t y = 0;
};

class GT911Touch {
 public:
  bool begin();
  bool readPoint(TouchPoint* point, bool* homeButtonPressed = nullptr);
  bool isAvailable() const { return available; }

 private:
  uint8_t address = 0x5D;
  bool probeAddress(uint8_t addr);
  bool available = false;
  bool writeReg8(uint16_t reg, uint8_t value);
  bool readReg(uint16_t reg, uint8_t* data, size_t len);
};

}  // namespace BoardEPD47

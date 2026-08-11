#include "BoardEPD47.h"

#include <cassert>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <SPI.h>
#include <Wire.h>
#include <epd_driver.h>

namespace BoardEPD47 {
namespace {
constexpr uint16_t GT911_PRODUCT_ID_REG = 0x8140;
constexpr uint16_t GT911_STATUS_REG = 0x814E;
constexpr uint16_t GT911_POINT1_REG = 0x814F;
constexpr uint8_t GT911_STATUS_READY = 0x80;
constexpr uint8_t GT911_STATUS_HAVE_KEY = 0x10;
constexpr uint8_t GT911_TOUCH_COUNT_MASK = 0x0F;
constexpr uint8_t GT911_PRIMARY_ADDR = 0x5D;
constexpr uint8_t GT911_BACKUP_ADDR = 0x14;

constexpr BoardCapabilities kCapabilities = {
    .hasBacklight = false,
    .hasDetailedBatteryTelemetry = false,
    .hasHardPowerOff = false,
    .hasTouchWake = false,
    .hasTouch = true,
    .hasRtc = true,
};

constexpr BatteryProfile kBatteryProfile = {};
SemaphoreHandle_t i2cMutex = nullptr;

SemaphoreHandle_t ensureI2CMutex() {
  if (i2cMutex == nullptr) {
    i2cMutex = xSemaphoreCreateRecursiveMutex();
    assert(i2cMutex != nullptr && "Failed to create I2C mutex");
  }
  return i2cMutex;
}

uint16_t batteryPercentForMv(const uint16_t millivolts) {
  struct CurvePoint {
    uint16_t millivolts;
    uint8_t percent;
  };
  static constexpr CurvePoint curve[] = {
      {3300, 0}, {3500, 5}, {3600, 10}, {3700, 25}, {3800, 45},
      {3900, 65}, {4000, 80}, {4100, 92}, {4200, 100},
  };

  if (millivolts <= curve[0].millivolts) {
    return curve[0].percent;
  }
  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
    if (millivolts <= curve[i].millivolts) {
      const uint32_t voltageOffset = millivolts - curve[i - 1].millivolts;
      const uint32_t voltageSpan = curve[i].millivolts - curve[i - 1].millivolts;
      const uint32_t percentSpan = curve[i].percent - curve[i - 1].percent;
      return curve[i - 1].percent + (voltageOffset * percentSpan + voltageSpan / 2) / voltageSpan;
    }
  }
  return 100;
}

uint16_t readBatteryMillivolts() {
  // GPIO14's divider is supplied by the EPD power domain on this board.
  epd_poweron();
  delay(10);
  analogSetPinAttenuation(EPD47_BATTERY_ADC, ADC_11db);
  uint32_t pinMillivolts = 0;
  constexpr uint8_t sampleCount = 8;
  for (uint8_t i = 0; i < sampleCount; ++i) {
    pinMillivolts += analogReadMilliVolts(EPD47_BATTERY_ADC);
  }
  pinMillivolts = (pinMillivolts + sampleCount / 2) / sampleCount;
  epd_poweroff_all();
  return static_cast<uint16_t>(min<uint32_t>(pinMillivolts * 2U, 5000U));
}
}  // namespace

const char* id() { return "lilygo-epd47-s3"; }

const char* displayName() { return "LilyGo EPD47 ESP32-S3"; }

const char* firmwareMarker() { return "CROSSPOINT_BOARD_ID:lilygo-epd47-s3"; }

const BoardCapabilities& capabilities() { return kCapabilities; }

ScopedI2CLock::ScopedI2CLock() {
  xSemaphoreTakeRecursive(ensureI2CMutex(), portMAX_DELAY);
  locked_ = true;
}

ScopedI2CLock::~ScopedI2CLock() {
  if (locked_) {
    xSemaphoreGiveRecursive(ensureI2CMutex());
    locked_ = false;
  }
}

void beginI2C() {
  ensureI2CMutex();
  Wire.begin(EPD47_I2C_SDA, EPD47_I2C_SCL);
  Wire.setClock(EPD47_I2C_FREQ);
  Wire.setTimeOut(50);
}

void initBacklight() {}

void setBacklightLevel(uint8_t level) { (void)level; }

void prepareSdBus() {
  pinMode(EPD47_SD_CS, OUTPUT);
  digitalWrite(EPD47_SD_CS, HIGH);
  SPI.begin(EPD47_SD_SCLK, EPD47_SD_MISO, EPD47_SD_MOSI, EPD47_SD_CS);
}

void disableGpsLora() {}

void begin() {
  beginI2C();
  pinMode(EPD47_BUTTON, INPUT_PULLUP);
  pinMode(EPD47_TOUCH_INT, INPUT_PULLUP);
  pinMode(EPD47_BATTERY_ADC, INPUT);
  prepareSdBus();
}

void deinitForSleep() {
  pinMode(EPD47_SD_CS, INPUT);
  pinMode(EPD47_SD_MISO, INPUT);
  pinMode(EPD47_SD_MOSI, INPUT);
  pinMode(EPD47_SD_SCLK, INPUT);
  pinMode(EPD47_I2C_SDA, OPEN_DRAIN);
  pinMode(EPD47_I2C_SCL, OPEN_DRAIN);
  pinMode(EPD47_TOUCH_INT, INPUT);
}

const BatteryProfile& batteryProfile() { return kBatteryProfile; }

bool beginBatteryManagement() { return true; }

bool isBatteryManagementReady() { return true; }

bool readBatteryState(BatteryState* state) {
  if (state == nullptr) {
    return false;
  }
  *state = {};
  state->gaugeReady = true;
  state->gaugeReadOk = true;
  state->gaugeVoltageMv = readBatteryMillivolts();
  state->batteryVoltageMv = state->gaugeVoltageMv;
  state->socPercent = batteryPercentForMv(state->gaugeVoltageMv);
  state->gaugeState = state->socPercent >= 99 ? BatteryGaugeState::Full : BatteryGaugeState::Discharge;
  return true;
}

bool shutdownBatteryPower() { return false; }

bool pca9535Present() { return false; }

bool readPca9535Pin(uint8_t pin, bool* high) {
  (void)pin;
  if (high) {
    *high = false;
  }
  return false;
}

bool writePca9535Pin(uint8_t pin, bool high) {
  (void)pin;
  (void)high;
  return false;
}

bool setPca9535PinMode(uint8_t pin, uint8_t mode) {
  (void)pin;
  (void)mode;
  return false;
}

bool readButton() { return false; }

bool readBQ27220Reg16(uint8_t reg, uint16_t* value) {
  (void)reg;
  (void)value;
  return false;
}

bool readBQ25896Reg8(uint8_t reg, uint8_t* value) {
  (void)reg;
  (void)value;
  return false;
}

bool readBatteryStateOfCharge(uint16_t* soc) {
  if (soc == nullptr) {
    return false;
  }
  const uint16_t millivolts = readBatteryMillivolts();
  *soc = batteryPercentForMv(millivolts);
  return true;
}

bool readBatteryCurrentMa(int16_t* current) {
  (void)current;
  return false;
}

bool readBatteryAverageCurrentMa(int16_t* current) {
  (void)current;
  return false;
}

bool isUsbConnected() { return static_cast<bool>(Serial); }

bool GT911Touch::writeReg8(const uint16_t reg, const uint8_t value) {
  ScopedI2CLock lock;
  Wire.beginTransmission(address);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool GT911Touch::readReg(const uint16_t reg, uint8_t* data, const size_t len) {
  if (data == nullptr || len == 0 || len > 255) {
    return false;
  }
  ScopedI2CLock lock;
  Wire.beginTransmission(address);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const uint8_t requested = static_cast<uint8_t>(len);
  if (Wire.requestFrom(address, requested) != requested) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

bool GT911Touch::probeAddress(const uint8_t addr) {
  address = addr;
  uint8_t productId[4] = {};
  available = readReg(GT911_PRODUCT_ID_REG, productId, sizeof(productId));
  if (available) {
    writeReg8(GT911_STATUS_REG, 0);
  }
  return available;
}

bool GT911Touch::begin() {
  // EPD47 ties RESET high in hardware. Drive INT high briefly to wake a
  // controller that may have been left asleep before probing both addresses.
  pinMode(EPD47_TOUCH_INT, OUTPUT);
  digitalWrite(EPD47_TOUCH_INT, HIGH);
  delay(5);
  pinMode(EPD47_TOUCH_INT, INPUT_PULLUP);
  delay(50);

  if (probeAddress(GT911_PRIMARY_ADDR) || probeAddress(GT911_BACKUP_ADDR)) {
    return true;
  }
  address = GT911_PRIMARY_ADDR;
  available = false;
  return false;
}

bool GT911Touch::readPoint(TouchPoint* point, bool* homeButtonPressed) {
  if (homeButtonPressed) {
    *homeButtonPressed = false;
  }
  if (!available || point == nullptr) {
    return false;
  }

  uint8_t status = 0;
  if (!readReg(GT911_STATUS_REG, &status, 1) || (status & GT911_STATUS_READY) == 0) {
    return false;
  }
  if (homeButtonPressed) {
    *homeButtonPressed = (status & GT911_STATUS_HAVE_KEY) != 0;
  }
  if ((status & GT911_TOUCH_COUNT_MASK) == 0) {
    writeReg8(GT911_STATUS_REG, 0);
    return false;
  }

  uint8_t data[8] = {};
  const bool ok = readReg(GT911_POINT1_REG, data, sizeof(data));
  writeReg8(GT911_STATUS_REG, 0);
  if (!ok) {
    return false;
  }

  const uint16_t rawX = static_cast<uint16_t>(data[1]) | (static_cast<uint16_t>(data[2]) << 8);
  const uint16_t rawY = static_cast<uint16_t>(data[3]) | (static_cast<uint16_t>(data[4]) << 8);

  // Match LilyGo's GT911 setup: swap XY, then mirror the physical Y axis.
  point->x = min<uint16_t>(rawY, EPD47_WIDTH - 1);
  point->y = rawX < EPD47_HEIGHT ? EPD47_HEIGHT - 1 - rawX : 0;
  return true;
}

}  // namespace BoardEPD47

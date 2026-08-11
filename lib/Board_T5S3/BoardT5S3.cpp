#include "BoardT5S3.h"

#include <cassert>
#include <bq25896.h>
#include <bq25896_hal_esp_idf.h>
#include <bq27220.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <SPI.h>
#include <Wire.h>

namespace BoardT5S3 {
namespace {
constexpr uint8_t PCA_REG_INPUT0 = 0x00;
constexpr uint8_t PCA_REG_OUTPUT0 = 0x02;
constexpr uint8_t PCA_REG_CONFIG0 = 0x06;
constexpr uint8_t kBacklightPwmChannel = 0;
constexpr uint8_t kBacklightPwmResolutionBits = 8;
constexpr uint32_t kBacklightPwmFrequencyHz = 5000;

constexpr BatteryProfile kBatteryProfile = {
    .inputLimitMa = 1000,
    .capacityMah = 1500,
    .chargeCurrentMa = 512,
    .prechargeCurrentMa = 64,
    .terminationCurrentMa = 64,
    .chargeVoltageMv = 4208,
    .chargeTerminationVoltageDeltaMv = 100,
    .systemMinVoltageMv = 3300,
    .currentThresholdMa = 20,
};

constexpr BoardCapabilities kCapabilities = {
    .hasBacklight = true,
    .hasDetailedBatteryTelemetry = true,
    .hasHardPowerOff = true,
    .hasTouchWake = true,
    .hasTouch = true,
    .hasRtc = true,
};

bool batteryInitAttempted = false;
bool bq25896Ready = false;
bool bq27220Ready = false;
bq25896_hal_esp_idf_ctx_t bq25896HalCtx = {};
bq25896_t bq25896 = {};
BQ27220 bq27220;
bool backlightInitialized = false;
SemaphoreHandle_t i2cMutex = nullptr;

constexpr uint16_t GT911_PRODUCT_ID_REG = 0x8140;
constexpr uint16_t GT911_STATUS_REG = 0x814E;
constexpr uint16_t GT911_POINT1_REG = 0x814F;
constexpr uint8_t GT911_STATUS_READY = 0x80;
constexpr uint8_t GT911_STATUS_HAVE_KEY = 0x10;
constexpr uint8_t GT911_TOUCH_COUNT_MASK = 0x0F;
constexpr uint8_t GT911_BACKUP_ADDR = 0x14;

SemaphoreHandle_t ensureI2CMutex() {
  if (i2cMutex == nullptr) {
    i2cMutex = xSemaphoreCreateRecursiveMutex();
    assert(i2cMutex != nullptr && "Failed to create I2C mutex");
  }
  return i2cMutex;
}

bool i2cWriteReg(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) {
  ScopedI2CLock lock;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (data != nullptr && len > 0) {
    Wire.write(data, len);
  }
  return Wire.endTransmission() == 0;
}

bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
  ScopedI2CLock lock;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const uint8_t requested = static_cast<uint8_t>(len);
  if (Wire.requestFrom(addr, requested) != requested) {
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

bool updatePca9535Bit(uint8_t baseReg, uint8_t pin, bool high) {
  const uint8_t port = pin / 8;
  const uint8_t bit = pin % 8;
  uint8_t value = 0;
  if (!i2cReadReg(T5S3_PCA9535_ADDR, baseReg + port, &value, 1)) {
    return false;
  }
  if (high) {
    value |= static_cast<uint8_t>(1U << bit);
  } else {
    value &= static_cast<uint8_t>(~(1U << bit));
  }
  return i2cWriteReg(T5S3_PCA9535_ADDR, baseReg + port, &value, 1);
}

bool readReg16LE(uint8_t addr, uint8_t reg, uint16_t* value) {
  uint8_t data[2] = {0, 0};
  if (!i2cReadReg(addr, reg, data, sizeof(data))) {
    return false;
  }
  *value = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  return true;
}

i2c_master_bus_handle_t i2cMasterBusHandle() { return reinterpret_cast<i2c_master_bus_handle_t>(&Wire); }

bool applyBq25896Step(bq25896_err_t err) { return BQ25896_SUCCEEDED(err); }

uint8_t backlightDutyForLevel(uint8_t level) {
  if (level == 0) {
    return 0;
  }
  if (level > 10) {
    level = 10;
  }

  const uint32_t levelSquared = static_cast<uint32_t>(level) * static_cast<uint32_t>(level);
  const uint32_t duty = (levelSquared * 255U + 50U) / 100U;
  return static_cast<uint8_t>(duty > 255U ? 255U : duty);
}

bool configureBq25896() {
  bq25896_config_t config = {};
  if (BQ25896_FAILED(bq25896_get_default_config(&config))) {
    return false;
  }

  if (BQ25896_FAILED(bq25896_hal_esp_idf_get_default_ctx(&bq25896HalCtx))) {
    return false;
  }
  bq25896HalCtx.scl_speed_hz = T5S3_I2C_FREQ;
  bq25896HalCtx.timeout_ms = 100;

  if (BQ25896_FAILED(bq25896_hal_esp_idf_ctx_init(&bq25896HalCtx, i2cMasterBusHandle(), T5S3_BQ25896_ADDR))) {
    return false;
  }

  if (BQ25896_FAILED(bq25896_hal_esp_idf_make_hal(&bq25896HalCtx, &config.hal))) {
    (void)bq25896_hal_esp_idf_ctx_deinit(&bq25896HalCtx);
    return false;
  }

  config.i2c_addr_7bit = T5S3_BQ25896_ADDR;
  config.reset_registers_on_init = true;
  config.exit_hiz_on_init = true;
  config.adc_mode = BQ25896_ADC_MODE_CONTINUOUS;
  config.watchdog = BQ25896_WATCHDOG_DISABLED;

  if (BQ25896_FAILED(bq25896_init(&bq25896, &config))) {
    (void)bq25896_hal_esp_idf_ctx_deinit(&bq25896HalCtx);
    return false;
  }

  const bool ok = applyBq25896Step(bq25896_disable_otg(&bq25896)) &&
                  applyBq25896Step(bq25896_enable_battery_power_path(&bq25896)) &&
                  applyBq25896Step(bq25896_set_input_limit_ma(&bq25896, kBatteryProfile.inputLimitMa)) &&
                  applyBq25896Step(bq25896_set_charge_current_ma(&bq25896, kBatteryProfile.chargeCurrentMa)) &&
                  applyBq25896Step(bq25896_set_precharge_current_ma(&bq25896, kBatteryProfile.prechargeCurrentMa)) &&
                  applyBq25896Step(bq25896_set_termination_current_ma(&bq25896,
                                                                       kBatteryProfile.terminationCurrentMa)) &&
                  applyBq25896Step(bq25896_set_charge_voltage_mv(&bq25896, kBatteryProfile.chargeVoltageMv)) &&
                  applyBq25896Step(bq25896_set_system_min_voltage_mv(&bq25896,
                                                                      kBatteryProfile.systemMinVoltageMv)) &&
                  applyBq25896Step(bq25896_enable_charge(&bq25896));
  if (!ok) {
    (void)bq25896_hal_esp_idf_ctx_deinit(&bq25896HalCtx);
    bq25896 = {};
  }
  return ok;
}

bool configureBq27220() {
  if (!bq27220.begin(i2cMasterBusHandle(), T5S3_BQ27220_ADDR, T5S3_I2C_FREQ)) {
    return false;
  }

  if (!bq27220.setDefaultCapacity(kBatteryProfile.capacityMah) ||
      !bq27220.setChargeParameters(kBatteryProfile.chargeCurrentMa, kBatteryProfile.chargeVoltageMv,
                                   kBatteryProfile.terminationCurrentMa,
                                   kBatteryProfile.chargeTerminationVoltageDeltaMv) ||
      !bq27220.init()) {
    bq27220.end();
    return false;
  }
  return true;
}

}  // namespace

const char* id() { return "t5s3-pro"; }

const char* displayName() { return "LilyGo T5S3 E-Paper PRO/Lite"; }

const char* firmwareMarker() { return "CROSSPOINT_BOARD_ID:t5s3-pro"; }

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
  Wire.begin(T5S3_SDA, T5S3_SCL);
  Wire.setClock(T5S3_I2C_FREQ);
  Wire.setTimeOut(50);
}

void initBacklight() {
  if (backlightInitialized) {
    return;
  }

  ledcSetup(kBacklightPwmChannel, kBacklightPwmFrequencyHz, kBacklightPwmResolutionBits);
  ledcAttachPin(T5S3_BL_EN, kBacklightPwmChannel);
  backlightInitialized = true;
  ledcWrite(kBacklightPwmChannel, 0);
}

void setBacklightLevel(uint8_t level) {
  if (!backlightInitialized) {
    initBacklight();
  }
  ledcWrite(kBacklightPwmChannel, backlightDutyForLevel(level));
}

void prepareSdBus() {
  pinMode(T5S3_LORA_CS, OUTPUT);
  digitalWrite(T5S3_LORA_CS, HIGH);
  pinMode(T5S3_SD_CS, OUTPUT);
  digitalWrite(T5S3_SD_CS, HIGH);
  SPI.begin(T5S3_SPI_SCLK, T5S3_SPI_MISO, T5S3_SPI_MOSI, T5S3_SD_CS);
}

void disableGpsLora() {
  pinMode(T5S3_LORA_CS, OUTPUT);
  digitalWrite(T5S3_LORA_CS, HIGH);
  pinMode(T5S3_LORA_RST, OUTPUT);
  digitalWrite(T5S3_LORA_RST, LOW);
  pinMode(T5S3_LORA_IRQ, INPUT);
  pinMode(T5S3_LORA_BUSY, INPUT);
  pinMode(T5S3_GPS_RXD, INPUT);
  pinMode(T5S3_GPS_TXD, INPUT);

  writePca9535Pin(PCA9535_IO00_LORA_GPS_EN, false);
  setPca9535PinMode(PCA9535_IO00_LORA_GPS_EN, OUTPUT);
}

void begin() {
  beginI2C();
  initBacklight();
  setBacklightLevel(0);

  pinMode(T5S3_BOOT_BTN, INPUT_PULLUP);
  if (T5S3_PCA9535_INT > 0) {
    pinMode(T5S3_PCA9535_INT, INPUT_PULLUP);
  }

  prepareSdBus();
  disableGpsLora();
}

void deinitForSleep() {
  setBacklightLevel(0);
  pinMode(T5S3_BL_EN, OUTPUT);
  digitalWrite(T5S3_BL_EN, LOW);
  disableGpsLora();
  pinMode(T5S3_SD_CS, INPUT);
  pinMode(T5S3_GPS_RXD, INPUT);
  pinMode(T5S3_GPS_TXD, INPUT);
}

const BatteryProfile& batteryProfile() { return kBatteryProfile; }

bool beginBatteryManagement() {
  if (batteryInitAttempted) {
    return bq25896Ready || bq27220Ready;
  }

  batteryInitAttempted = true;
  bq25896Ready = configureBq25896();
  bq27220Ready = configureBq27220();
  return bq25896Ready || bq27220Ready;
}

bool isBatteryManagementReady() { return bq25896Ready || bq27220Ready; }

bool shutdownBatteryPower() {
  if (!beginBatteryManagement() || !bq25896Ready) {
    return false;
  }

  bq25896_status_t chargerStatus = {};
  const bq25896_err_t statusRc = bq25896_read_status(&bq25896, &chargerStatus);
  if (BQ25896_FAILED(statusRc) || chargerStatus.vbus_good || chargerStatus.power_good) {
    return false;
  }

  return BQ25896_SUCCEEDED(bq25896_shutdown(&bq25896));
}

bool readBatteryState(BatteryState* state) {
  if (state == nullptr) {
    return false;
  }

  *state = {};
  state->chargerReady = bq25896Ready;
  state->gaugeReady = bq27220Ready;
  state->gaugeChargeVoltageMv = kBatteryProfile.chargeVoltageMv;
  state->gaugeTaperCurrentMa = kBatteryProfile.terminationCurrentMa;

  if (bq25896Ready) {
    bq25896_status_t chargerStatus = {};
    bq25896_adc_t chargerAdc = {};
    bq25896_charge_config_t chargerConfig = {};
    const bq25896_err_t statusRc = bq25896_read_status(&bq25896, &chargerStatus);
    const bq25896_err_t adcRc = bq25896_read_adc(&bq25896, &chargerAdc);
    const bq25896_err_t configRc = bq25896_read_charge_config(&bq25896, &chargerConfig);
    state->chargerReadOk = BQ25896_SUCCEEDED(statusRc) && BQ25896_SUCCEEDED(adcRc) && BQ25896_SUCCEEDED(configRc);
    if (state->chargerReadOk) {
      state->vbusConnected = chargerStatus.vbus_good || chargerStatus.power_good;
      state->chargeEnabled = chargerConfig.charge_enabled;
      state->chargerVbusStatus = static_cast<uint8_t>(chargerStatus.vbus_status);
      state->chargerStatus = static_cast<BatteryChargeStatus>(chargerStatus.charge_status);
      state->inputLimitMa = chargerStatus.input_limit_ma;
      state->chargeCurrentMa = chargerConfig.charge_current_ma;
      state->prechargeCurrentMa = chargerConfig.precharge_current_ma;
      state->terminationCurrentMa = chargerConfig.termination_current_ma;
      state->chargerAdcCurrentMa = chargerAdc.charge_current_ma;
      state->chargeVoltageMv = chargerConfig.charge_voltage_mv;
      state->systemVoltageMv = chargerAdc.system_voltage_mv;
      state->batteryVoltageMv = chargerAdc.battery_voltage_mv;
      state->vbusVoltageMv = chargerAdc.vbus_voltage_mv;
      state->gaugeChargeVoltageMv = chargerConfig.charge_voltage_mv;
      state->gaugeTaperCurrentMa = chargerConfig.termination_current_ma;
      state->chargeDone = chargerStatus.charge_status == BQ25896_CHARGE_STATUS_TERMINATION_DONE;
      state->charging = state->chargeEnabled &&
                        (chargerStatus.charge_status == BQ25896_CHARGE_STATUS_PRECHARGE ||
                         chargerStatus.charge_status == BQ25896_CHARGE_STATUS_FAST_CHARGE);
    }
  }

  if (bq27220Ready) {
    BQ27220Snapshot gauge = {};
    state->gaugeReadOk = bq27220.readSnapshot(&gauge);
    if (state->gaugeReadOk) {
      const bool inferredVbus = state->vbusConnected || gauge.charging;
      state->gaugeState =
          static_cast<BatteryGaugeState>(BQ27220::classifyState(&gauge, inferredVbus, kBatteryProfile.currentThresholdMa));
      state->gaugeBatteryFullFlag = gauge.battery_status.reg.FC;
      state->gaugeGaugingFullFlag = gauge.gauging_status.reg.FC;
      state->gaugeTaperFlag = gauge.battery_status.reg.TCA;
      state->gaugeChargeInhibit = gauge.battery_status.reg.CHGINH;
      state->gaugeVoltageMv = gauge.voltage_mv;
      state->currentMa = gauge.current_ma;
      state->averageCurrentMa = gauge.average_current_ma;
      state->socPercent = gauge.soc;
      state->sohPercent = gauge.soh_percent;
      state->fullCapacityMah = gauge.fcc_mah;
      state->remainingCapacityMah = gauge.remaining_capacity_mah;
      state->temperatureDk = gauge.temperature_dk;
      state->batteryStatusRaw = gauge.battery_status.full;
      state->gaugingStatusRaw = gauge.gauging_status.full;
      state->charging = state->charging || gauge.charging;
      state->chargeDone = state->chargeDone || gauge.full || gauge.battery_status.reg.TCA;
      if (!state->chargerReadOk) {
        state->vbusConnected = inferredVbus;
      }
    }
  }

  return state->chargerReadOk || state->gaugeReadOk;
}

bool pca9535Present() {
  ScopedI2CLock lock;
  Wire.beginTransmission(T5S3_PCA9535_ADDR);
  return Wire.endTransmission() == 0;
}

bool setPca9535PinMode(uint8_t pin, uint8_t mode) {
  const bool inputMode = mode != OUTPUT;
  return updatePca9535Bit(PCA_REG_CONFIG0, pin, inputMode);
}

bool writePca9535Pin(uint8_t pin, bool high) {
  return updatePca9535Bit(PCA_REG_OUTPUT0, pin, high);
}

bool readPca9535Pin(uint8_t pin, bool* high) {
  if (!high) {
    return false;
  }
  const uint8_t port = pin / 8;
  const uint8_t bit = pin % 8;
  uint8_t value = 0;
  if (!i2cReadReg(T5S3_PCA9535_ADDR, PCA_REG_INPUT0 + port, &value, 1)) {
    return false;
  }
  *high = (value & (1U << bit)) != 0;
  return true;
}

bool readButton() {
  bool high = true;
  setPca9535PinMode(PCA9535_IO12_BUTTON, INPUT);
  if (!readPca9535Pin(PCA9535_IO12_BUTTON, &high)) {
    return false;
  }
  return !high;
}

bool readBQ27220Reg16(uint8_t reg, uint16_t* value) {
  if (!value) {
    return false;
  }
  return readReg16LE(T5S3_BQ27220_ADDR, reg, value);
}

bool readBQ25896Reg8(uint8_t reg, uint8_t* value) {
  if (!value) {
    return false;
  }
  return i2cReadReg(T5S3_BQ25896_ADDR, reg, value, 1);
}

bool readBatteryStateOfCharge(uint16_t* soc) {
  return readBQ27220Reg16(CommandStateOfCharge, soc);
}

bool readBatteryCurrentMa(int16_t* current) {
  if (!current) {
    return false;
  }
  uint16_t raw = 0;
  if (!readBQ27220Reg16(CommandCurrent, &raw)) {
    return false;
  }
  *current = static_cast<int16_t>(raw);
  return true;
}

bool readBatteryAverageCurrentMa(int16_t* current) {
  if (!current) {
    return false;
  }
  uint16_t raw = 0;
  if (!readBQ27220Reg16(CommandAverageCurrent, &raw)) {
    return false;
  }
  *current = static_cast<int16_t>(raw);
  return true;
}

bool isUsbConnected() {
  uint8_t systemStatus = 0;
  if (readBQ25896Reg8(BQ25896_REG_0B, &systemStatus) &&
      (systemStatus & (BQ25896_REG0B_VBUS_STAT_MASK | BQ25896_REG0B_PG_STAT_MASK)) != 0) {
    return true;
  }

  uint8_t vbusStatus = 0;
  if (readBQ25896Reg8(BQ25896_REG_11, &vbusStatus) && (vbusStatus & BQ25896_REG11_VBUS_GD_MASK) != 0) {
    return true;
  }

  int16_t currentMa = 0;
  int16_t averageCurrentMa = 0;
  if (readBatteryAverageCurrentMa(&averageCurrentMa)) {
    return averageCurrentMa > kBatteryProfile.currentThresholdMa;
  }
  return readBatteryCurrentMa(&currentMa) && currentMa > kBatteryProfile.currentThresholdMa;
}

bool GT911Touch::writeReg8(uint16_t reg, uint8_t value) {
  ScopedI2CLock lock;
  Wire.beginTransmission(address);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool GT911Touch::readReg(uint16_t reg, uint8_t* data, size_t len) {
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

void GT911Touch::resetForAddress(uint8_t addr) {
  // GT911 samples INT while RESET is released to select its I2C address.
  // INT low selects 0x5D; INT high selects 0x14.
  pinMode(T5S3_TOUCH_INT, OUTPUT);
  digitalWrite(T5S3_TOUCH_INT, addr == T5S3_GT911_ADDR ? LOW : HIGH);
  pinMode(T5S3_TOUCH_RST, OUTPUT);
  digitalWrite(T5S3_TOUCH_RST, LOW);
  delay(20);
  digitalWrite(T5S3_TOUCH_RST, HIGH);
  delay(60);
  pinMode(T5S3_TOUCH_INT, INPUT);
  delay(5);
}

bool GT911Touch::probeAddress(uint8_t addr) {
  address = addr;
  uint8_t productId[4] = {0, 0, 0, 0};
  available = readReg(GT911_PRODUCT_ID_REG, productId, sizeof(productId));
  if (available) {
    writeReg8(GT911_STATUS_REG, 0);
  }
  return available;
}

bool GT911Touch::begin() {
  resetForAddress(T5S3_GT911_ADDR);
  if (probeAddress(T5S3_GT911_ADDR)) {
    return true;
  }

  resetForAddress(GT911_BACKUP_ADDR);
  if (probeAddress(GT911_BACKUP_ADDR)) {
    return true;
  }

  address = T5S3_GT911_ADDR;
  available = false;
  return false;
}

bool GT911Touch::readPoint(TouchPoint* point, bool* homeButtonPressed) {
  if (!available || point == nullptr) {
    if (homeButtonPressed) {
      *homeButtonPressed = false;
    }
    return false;
  }

  uint8_t status = 0;
  if (!readReg(GT911_STATUS_REG, &status, 1)) {
    if (homeButtonPressed) {
      *homeButtonPressed = false;
    }
    return false;
  }
  if ((status & GT911_STATUS_READY) == 0) {
    if (homeButtonPressed) {
      *homeButtonPressed = false;
    }
    return false;
  }

  if (homeButtonPressed) {
    *homeButtonPressed = (status & GT911_STATUS_HAVE_KEY) != 0;
  }

  const uint8_t touchCount = status & GT911_TOUCH_COUNT_MASK;
  if (touchCount == 0) {
    writeReg8(GT911_STATUS_REG, 0);
    return false;
  }

  uint8_t data[8] = {0};
  const bool ok = readReg(GT911_POINT1_REG, data, sizeof(data));
  writeReg8(GT911_STATUS_REG, 0);
  if (!ok) {
    return false;
  }

  point->x = static_cast<uint16_t>(data[1]) | (static_cast<uint16_t>(data[2]) << 8);
  point->y = static_cast<uint16_t>(data[3]) | (static_cast<uint16_t>(data[4]) << 8);
  return true;
}

}  // namespace BoardT5S3

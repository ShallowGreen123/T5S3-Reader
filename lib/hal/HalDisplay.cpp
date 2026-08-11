#include <HalDisplay.h>

#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)

#include <Board.h>
#include <Logging.h>
#include <M5GFX.h>
#include <lgfx/v1/platforms/esp32/Bus_EPD.h>
#include <lgfx/v1/platforms/esp32/Panel_EPD.hpp>
#include <Wire.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

// Global HalDisplay instance
HalDisplay display;

namespace {
constexpr uint32_t kEpdBusHz = 16000000;
constexpr uint32_t kMiddleRefreshThreshold = 8;
constexpr uint32_t kQualityRefreshThreshold = 18;
constexpr int kDefaultVcomMv = -1600;
constexpr int kReaderTurnStandardSliceCount = 24;
constexpr int kReaderTurnFastSliceCount = 30;
constexpr int kReaderTurnMinSliceHeight = 6;

constexpr uint8_t kTpsRegEnable = 0x01;
constexpr uint8_t kTpsRegVcom = 0x03;
constexpr uint8_t kTpsRegPowerGood = 0x0F;
constexpr uint8_t kTpsEnableOutputs = 0x3F;

constexpr uint8_t kGrayBlack = 0x00;
constexpr uint8_t kGrayDark = 0x55;
constexpr uint8_t kGrayLight = 0xAA;
constexpr uint8_t kGrayWhite = 0xFF;

bool writeTpsRegister(const uint8_t reg, const uint8_t* data, const size_t len) {
  Board::ScopedI2CLock lock;
  Wire.beginTransmission(T5S3_TPS65185_ADDR);
  Wire.write(reg);
  if (data != nullptr && len > 0) {
    Wire.write(data, len);
  }
  return Wire.endTransmission() == 0;
}

bool writeTpsRegister8(const uint8_t reg, const uint8_t value) { return writeTpsRegister(reg, &value, 1); }

bool readTpsRegister(const uint8_t reg, uint8_t* data, const size_t len) {
  if (data == nullptr || len == 0) {
    return false;
  }

  Board::ScopedI2CLock lock;
  Wire.beginTransmission(T5S3_TPS65185_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const uint8_t requested = static_cast<uint8_t>(len);
  if (Wire.requestFrom(static_cast<uint8_t>(T5S3_TPS65185_ADDR), requested) != requested) {
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

bool waitForPcaPinHigh(const uint8_t pin, const uint32_t timeoutMs) {
  const uint32_t start = millis();
  bool high = false;
  while (millis() - start < timeoutMs) {
    if (Board::readPca9535Pin(pin, &high) && high) {
      return true;
    }
    delay(1);
  }
  return false;
}

bool waitForTpsReady(const uint32_t timeoutMs) {
  const uint32_t start = millis();
  uint8_t powerGood = 0;
  while (millis() - start < timeoutMs) {
    if (readTpsRegister(kTpsRegPowerGood, &powerGood, 1) && (powerGood & 0xFA) == 0xFA) {
      return true;
    }
    delay(1);
  }
  return false;
}

class T5S3BusEPD : public lgfx::Bus_EPD {
 public:
  bool init() override {
    if (!preparePowerPins()) {
      LOG_ERR("DSP", "Failed to configure PCA9535 power pins");
      return false;
    }
    return lgfx::Bus_EPD::init();
  }

  bool powerControl(const bool powerOn) override {
    if (_pwr_on == powerOn) {
      return true;
    }

    wait();
    if (powerOn) {
      return powerOnSequence();
    }

    powerOffSequence();
    return true;
  }

 private:
  bool preparePowerPins() {
    // Keep the full expander setup sequence on the bus atomically. The render
    // task can power-cycle EPD rails while the main loop is polling RTC/touch.
    Board::ScopedI2CLock busLock;
    bool ok = true;
    ok &= Board::setPca9535PinMode(PCA9535_IO10_EP_OE, OUTPUT);
    ok &= Board::setPca9535PinMode(PCA9535_IO11_EP_MODE, OUTPUT);
    ok &= Board::setPca9535PinMode(PCA9535_IO13_TPS_PWRUP, OUTPUT);
    ok &= Board::setPca9535PinMode(PCA9535_IO14_VCOM_CTRL, OUTPUT);
    ok &= Board::setPca9535PinMode(PCA9535_IO15_TPS_WAKEUP, OUTPUT);
    ok &= Board::setPca9535PinMode(PCA9535_IO16_TPS_PWR_GOOD, INPUT);
    ok &= Board::setPca9535PinMode(PCA9535_IO17_TPS_INT, INPUT);

    ok &= Board::writePca9535Pin(PCA9535_IO10_EP_OE, false);
    ok &= Board::writePca9535Pin(PCA9535_IO11_EP_MODE, false);
    ok &= Board::writePca9535Pin(PCA9535_IO13_TPS_PWRUP, false);
    ok &= Board::writePca9535Pin(PCA9535_IO14_VCOM_CTRL, false);
    ok &= Board::writePca9535Pin(PCA9535_IO15_TPS_WAKEUP, false);
    return ok;
  }

  bool powerOnSequence() {
    // Hold the I2C bus for the whole EPD power-on sequence so RTC/touch reads
    // cannot interleave with the PCA9535/TPS65185 transactions.
    Board::ScopedI2CLock busLock;
    const auto& cfg = config();

    lgfx::gpio_hi(cfg.pin_spv);

    bool ok = true;
    ok &= Board::writePca9535Pin(PCA9535_IO10_EP_OE, true);
    ok &= Board::writePca9535Pin(PCA9535_IO11_EP_MODE, true);
    ok &= Board::writePca9535Pin(PCA9535_IO15_TPS_WAKEUP, true);
    ok &= Board::writePca9535Pin(PCA9535_IO13_TPS_PWRUP, true);
    ok &= Board::writePca9535Pin(PCA9535_IO14_VCOM_CTRL, true);
    if (!ok) {
      LOG_ERR("DSP", "Failed to assert EPD power rails");
      powerOffSequence();
      return false;
    }

    delay(1);
    if (!waitForPcaPinHigh(PCA9535_IO16_TPS_PWR_GOOD, 400)) {
      LOG_ERR("DSP", "TPS65185 power-good pin did not assert");
      powerOffSequence();
      return false;
    }

    if (!writeTpsRegister8(kTpsRegEnable, kTpsEnableOutputs)) {
      LOG_ERR("DSP", "TPS65185 enable write failed");
      powerOffSequence();
      return false;
    }

    const uint16_t vcomValue = static_cast<uint16_t>(-kDefaultVcomMv / 10);
    const uint8_t vcomBytes[2] = {
        static_cast<uint8_t>(vcomValue & 0xFF),
        static_cast<uint8_t>(vcomValue >> 8),
    };
    if (!writeTpsRegister(kTpsRegVcom, vcomBytes, sizeof(vcomBytes))) {
      LOG_ERR("DSP", "TPS65185 VCOM write failed");
      powerOffSequence();
      return false;
    }

    if (!waitForTpsReady(400)) {
      LOG_ERR("DSP", "TPS65185 rails never reached ready state");
      powerOffSequence();
      return false;
    }

    _pwr_on = true;
    return true;
  }

  void powerOffSequence() {
    Board::ScopedI2CLock busLock;
    const auto& cfg = config();

    Board::writePca9535Pin(PCA9535_IO10_EP_OE, false);
    Board::writePca9535Pin(PCA9535_IO11_EP_MODE, false);
    Board::writePca9535Pin(PCA9535_IO13_TPS_PWRUP, false);
    Board::writePca9535Pin(PCA9535_IO14_VCOM_CTRL, false);
    delay(1);
    Board::writePca9535Pin(PCA9535_IO15_TPS_WAKEUP, false);

    lgfx::gpio_lo(cfg.pin_spv);
    _pwr_on = false;
  }
};

uint8_t grayscaleValueForBit(const uint8_t baseByte, const uint8_t lsbByte, const uint8_t msbByte, const uint8_t mask) {
  if (baseByte & mask) {
    return kGrayWhite;
  }

  const bool lsb = (lsbByte & mask) != 0;
  const bool msb = (msbByte & mask) != 0;

  if (msb && lsb) {
    return kGrayDark;
  }
  if (msb) {
    return kGrayLight;
  }
  if (lsb) {
    return kGrayDark;
  }
  return kGrayBlack;
}

lgfx::epd_mode::epd_mode_t epdModeForRefreshMode(const HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return lgfx::epd_mode::epd_quality;
    case HalDisplay::HALF_REFRESH:
      return lgfx::epd_mode::epd_text;
    case HalDisplay::BALANCED_REFRESH:
      return lgfx::epd_mode::epd_fast;
    case HalDisplay::FAST_REFRESH:
    default:
      return lgfx::epd_mode::epd_fast;
  }
}
}  // namespace

class T5S3M5GfxDisplay : public lgfx::LGFX_Device {
 public:
  T5S3M5GfxDisplay() {
    auto busCfg = bus_.config();
    busCfg.bus_speed = kEpdBusHz;
    busCfg.pin_data[0] = EP_D0;
    busCfg.pin_data[1] = EP_D1;
    busCfg.pin_data[2] = EP_D2;
    busCfg.pin_data[3] = EP_D3;
    busCfg.pin_data[4] = EP_D4;
    busCfg.pin_data[5] = EP_D5;
    busCfg.pin_data[6] = EP_D6;
    busCfg.pin_data[7] = EP_D7;
    busCfg.pin_pwr = T5S3_LORA_CS;  // Required by esp_lcd i80 API, not used for real power control.
    busCfg.pin_sph = EP_STH;
    busCfg.pin_spv = EP_STV;
    busCfg.pin_oe = T5S3_LORA_CS;   // Dummy direct GPIO; OE itself is handled through the PCA9535.
    busCfg.pin_le = EP_LEH;
    busCfg.pin_cl = EP_CKH;
    busCfg.pin_ckv = EP_CKV;
    busCfg.bus_width = 8;
    bus_.config(busCfg);

    panel_.setBus(&bus_);

    auto detailCfg = panel_.config_detail();
    detailCfg.line_padding = 8;
    panel_.config_detail(detailCfg);

    auto panelCfg = panel_.config();
    panelCfg.memory_width = HalDisplay::DISPLAY_WIDTH;
    panelCfg.panel_width = HalDisplay::DISPLAY_WIDTH;
    panelCfg.memory_height = HalDisplay::DISPLAY_HEIGHT;
    panelCfg.panel_height = HalDisplay::DISPLAY_HEIGHT;
    panelCfg.offset_rotation = 0;
    panelCfg.offset_x = 0;
    panelCfg.offset_y = 0;
    panelCfg.bus_shared = false;
    panel_.config(panelCfg);

    setPanel(&panel_);
  }

 private:
  T5S3BusEPD bus_;
  lgfx::Panel_EPD panel_;
};

HalDisplay::HalDisplay() = default;

HalDisplay::~HalDisplay() {
  releaseBackend();
  free(frameBuffer);
  free(grayscaleLsbBuffer);
  free(grayscaleMsbBuffer);
  free(grayscaleBaseBuffer);
}

uint8_t* HalDisplay::allocatePlane() {
  auto* plane = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!plane) {
    plane = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  return plane;
}

void HalDisplay::releaseBackend() {
  delete panelCanvas;
  panelCanvas = nullptr;

  delete gfx;
  gfx = nullptr;

  displayReady = false;
}

bool HalDisplay::initializePanelCanvas() {
  panelCanvas = new lgfx::LGFX_Sprite(gfx);
  if (!panelCanvas) {
    return false;
  }

  panelCanvas->setPsram(true);
  panelCanvas->setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  return panelCanvas->createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT) != nullptr;
}

void HalDisplay::begin() {
  releaseBackend();
  Board::beginI2C();

  if (!frameBuffer) {
    frameBuffer = allocatePlane();
  }
  if (!frameBuffer) {
    LOG_ERR("DSP", "Failed to allocate logical framebuffer");
    return;
  }

  gfx = new T5S3M5GfxDisplay();
  if (!gfx) {
    LOG_ERR("DSP", "Failed to allocate M5GFX device");
    return;
  }

  if (!gfx->init()) {
    LOG_ERR("DSP", "M5GFX init failed");
    releaseBackend();
    return;
  }

  gfx->setRotation(0);
  gfx->setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  gfx->setEpdMode(lgfx::epd_mode::epd_fastest);
  gfx->powerSave(false);

  if (!initializePanelCanvas()) {
    LOG_ERR("DSP", "Failed to create M5GFX panel canvas");
    releaseBackend();
    return;
  }

  clearScreen(0xFF);
  memset(panelCanvas->getBuffer(), kGrayWhite, static_cast<size_t>(DISPLAY_WIDTH) * DISPLAY_HEIGHT);

  displayReady = true;
  forceFullRefresh = true;
  forcedRefreshPending = false;
  pendingDisplayEffect = EFFECT_NONE;
  refreshCycleCount = 0;
  grayscaleBaseCaptured = false;

  LOG_INF("DSP", "M5GFX T5S3 display initialized: %ux%u visible, %ux%u scan", VISIBLE_WIDTH, VISIBLE_HEIGHT,
          DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void HalDisplay::clearScreen(const uint8_t color) const {
  if (frameBuffer) {
    memset(frameBuffer, color, BUFFER_SIZE);
  }
}

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  if (!frameBuffer || !imageData || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }

  const uint16_t imageWidthBytes = w / 8;
  const uint16_t destByteX = x / 8;
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) {
      break;
    }
    const uint32_t destOffset = static_cast<uint32_t>(destY) * DISPLAY_WIDTH_BYTES + destByteX;
    const uint32_t srcOffset = static_cast<uint32_t>(row) * imageWidthBytes;
    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((destByteX + col) >= DISPLAY_WIDTH_BYTES) {
        break;
      }
      frameBuffer[destOffset + col] =
          fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
    }
  }
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  if (!frameBuffer || !imageData || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }

  const uint16_t imageWidthBytes = w / 8;
  const uint16_t destByteX = x / 8;
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) {
      break;
    }
    const uint32_t destOffset = static_cast<uint32_t>(destY) * DISPLAY_WIDTH_BYTES + destByteX;
    const uint32_t srcOffset = static_cast<uint32_t>(row) * imageWidthBytes;
    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((destByteX + col) >= DISPLAY_WIDTH_BYTES) {
        break;
      }
      const uint8_t srcByte = fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
      frameBuffer[destOffset + col] &= srcByte;
    }
  }
}

void HalDisplay::renderBwToPanelCanvas() const {
  if (!panelCanvas || !frameBuffer) {
    return;
  }

  auto* grayBuffer = static_cast<uint8_t*>(panelCanvas->getBuffer());
  if (!grayBuffer) {
    return;
  }

  for (uint16_t y = 0; y < DISPLAY_HEIGHT; ++y) {
    const uint8_t* srcRow = frameBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES;
    uint8_t* dstRow = grayBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH;
    for (uint16_t byteX = 0; byteX < DISPLAY_WIDTH_BYTES; ++byteX) {
      const uint8_t srcByte = srcRow[byteX];
      for (uint8_t bit = 0; bit < 8; ++bit) {
        dstRow[byteX * 8 + bit] = (srcByte & (0x80 >> bit)) ? kGrayWhite : kGrayBlack;
      }
    }
  }
}

void HalDisplay::renderGrayToPanelCanvas() const {
  if (!panelCanvas || !grayscaleLsbBuffer || !grayscaleMsbBuffer || !grayscaleBaseBuffer) {
    return;
  }

  auto* grayBuffer = static_cast<uint8_t*>(panelCanvas->getBuffer());
  if (!grayBuffer) {
    return;
  }

  for (uint16_t y = 0; y < DISPLAY_HEIGHT; ++y) {
    const uint8_t* baseRow = grayscaleBaseBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES;
    const uint8_t* lsbRow = grayscaleLsbBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES;
    const uint8_t* msbRow = grayscaleMsbBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES;
    uint8_t* dstRow = grayBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH;

    for (uint16_t byteX = 0; byteX < DISPLAY_WIDTH_BYTES; ++byteX) {
      const uint8_t baseByte = baseRow[byteX];
      const uint8_t lsbByte = lsbRow[byteX];
      const uint8_t msbByte = msbRow[byteX];

      for (uint8_t bit = 0; bit < 8; ++bit) {
        const uint8_t mask = static_cast<uint8_t>(0x80 >> bit);
        dstRow[byteX * 8 + bit] = grayscaleValueForBit(baseByte, lsbByte, msbByte, mask);
      }
    }
  }
}

void HalDisplay::pushPanelCanvasWithEffect(const DisplayEffect effect) const {
  if (!gfx || !panelCanvas) {
    return;
  }

  if (!panelCanvas->getBuffer()) {
    panelCanvas->pushSprite(0, 0);
    return;
  }

  int preferredSliceCount = 0;
  int slicesPerBatch = 1;
  bool reverse = false;
  switch (effect) {
    case EFFECT_READER_TURN_FORWARD_STANDARD:
      preferredSliceCount = kReaderTurnStandardSliceCount;
      slicesPerBatch = 2;
      reverse = false;
      break;
    case EFFECT_READER_TURN_BACKWARD_STANDARD:
      preferredSliceCount = kReaderTurnStandardSliceCount;
      slicesPerBatch = 2;
      reverse = true;
      break;
    case EFFECT_READER_TURN_FORWARD_FAST:
      preferredSliceCount = kReaderTurnFastSliceCount;
      slicesPerBatch = 3;
      reverse = false;
      break;
    case EFFECT_READER_TURN_BACKWARD_FAST:
      preferredSliceCount = kReaderTurnFastSliceCount;
      slicesPerBatch = 3;
      reverse = true;
      break;
    case EFFECT_NONE:
    default:
      panelCanvas->pushSprite(0, 0);
      return;
  }

  const int maxSliceCount = std::max(1, DISPLAY_HEIGHT / kReaderTurnMinSliceHeight);
  const int sliceCount = std::max(1, std::min(preferredSliceCount, maxSliceCount));
  const int sliceHeight = std::max(1, DISPLAY_HEIGHT / sliceCount);
  const int batchSize = std::max(1, slicesPerBatch);

  // The panel canvas is stored in physical scan orientation (960x540).
  // Sweeping physical Y from low->high maps to logical right->left in portrait,
  // which feels like a "next page" reveal. The reverse sweep gives prev page.
  for (int i = 0; i < sliceCount; ++i) {
    const int sliceIndex = reverse ? (sliceCount - 1 - i) : i;
    const int startRow = sliceIndex * sliceHeight;
    const int currentSliceHeight =
        (sliceIndex == sliceCount - 1) ? (DISPLAY_HEIGHT - startRow) : std::min(sliceHeight, DISPLAY_HEIGHT - startRow);
    if (currentSliceHeight <= 0) {
      continue;
    }

    gfx->setClipRect(0, startRow, DISPLAY_WIDTH, currentSliceHeight);
    panelCanvas->pushSprite(gfx, 0, 0);
    // if (((i + 1) % batchSize) == 0 || i == sliceCount - 1) {
    //   gfx->waitDisplay();
    // }
  }
  gfx->clearClipRect();
}

void HalDisplay::pushPanelCanvas(const RefreshMode mode, const lgfx::epd_mode::epd_mode_t epdMode) {
  gfx->waitDisplay();
  gfx->setEpdMode(epdMode);

  const DisplayEffect effect = (mode == FULL_REFRESH) ? EFFECT_NONE : pendingDisplayEffect;
  if (effect == EFFECT_NONE) {
    panelCanvas->pushSprite(0, 0);
  } else {
    pushPanelCanvasWithEffect(effect);
  }
  gfx->waitDisplay();
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  if (!displayReady || !gfx || !panelCanvas) {
    return;
  }
  (void)turnOffScreen;

  renderBwToPanelCanvas();

  if (forcedRefreshPending && (mode == FAST_REFRESH || mode == BALANCED_REFRESH)) {
    LOG_DBG("DSP", "Forcing requested refresh mode for next display update");
    mode = forcedRefreshMode;
  }
  if (forceFullRefresh && mode != FULL_REFRESH) {
    LOG_DBG("DSP", "Forcing full refresh for first display update");
    mode = FULL_REFRESH;
  }

  lgfx::epd_mode::epd_mode_t epdMode = lgfx::epd_mode::epd_fastest;
  if (mode == FULL_REFRESH) {
    epdMode = lgfx::epd_mode::epd_quality;
    refreshCycleCount = 0;
  } else if (mode == HALF_REFRESH) {
    epdMode = lgfx::epd_mode::epd_text;
    refreshCycleCount = 0;
  } else if (mode == BALANCED_REFRESH) {
    epdMode = lgfx::epd_mode::epd_fast;
    refreshCycleCount = 0;
  } else {
    const bool useQualityMode = refreshCycleCount >= kQualityRefreshThreshold;
    const bool useMiddleMode = !useQualityMode && refreshCycleCount >= kMiddleRefreshThreshold &&
                               (refreshCycleCount % kMiddleRefreshThreshold) == 0;
    if (useQualityMode) {
      epdMode = lgfx::epd_mode::epd_quality;
      refreshCycleCount = 0;
    } else {
      epdMode = useMiddleMode ? lgfx::epd_mode::epd_fast : lgfx::epd_mode::epd_fastest;
      refreshCycleCount++;
    }
  }

  pushPanelCanvas(mode, epdMode);

  forceFullRefresh = false;
  forcedRefreshPending = false;
  pendingDisplayEffect = EFFECT_NONE;
  grayscaleBaseCaptured = false;
}

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) { displayBuffer(mode, turnOffScreen); }

void HalDisplay::requestNextRefresh(HalDisplay::RefreshMode mode) {
  forcedRefreshMode = mode;
  forcedRefreshPending = true;
}

void HalDisplay::requestNextDisplayEffect(const DisplayEffect effect) { pendingDisplayEffect = effect; }

void HalDisplay::suppressInitialFullRefresh() { forceFullRefresh = false; }

void HalDisplay::deepSleep() {
  if (gfx) {
    gfx->waitDisplay();
    gfx->powerSave(true);
    gfx->sleep();
  }
  Board::deinitForSleep();
}

uint8_t* HalDisplay::getFrameBuffer() const { return frameBuffer; }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  copyGrayscaleLsbBuffers(lsbBuffer);
  copyGrayscaleMsbBuffers(msbBuffer);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  if (!lsbBuffer) {
    return;
  }
  if (!grayscaleLsbBuffer) {
    grayscaleLsbBuffer = allocatePlane();
  }
  if (grayscaleLsbBuffer) {
    memcpy(grayscaleLsbBuffer, lsbBuffer, BUFFER_SIZE);
  }
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (!msbBuffer) {
    return;
  }
  if (!grayscaleMsbBuffer) {
    grayscaleMsbBuffer = allocatePlane();
  }
  if (grayscaleMsbBuffer) {
    memcpy(grayscaleMsbBuffer, msbBuffer, BUFFER_SIZE);
  }
}

bool HalDisplay::captureGrayscaleBaseBuffer(const uint8_t* bwBuffer) {
  if (!bwBuffer) {
    return false;
  }
  if (!grayscaleBaseBuffer) {
    grayscaleBaseBuffer = allocatePlane();
  }
  if (!grayscaleBaseBuffer) {
    LOG_ERR("DSP", "Failed to allocate grayscale base buffer");
    grayscaleBaseCaptured = false;
    return false;
  }
  memcpy(grayscaleBaseBuffer, bwBuffer, BUFFER_SIZE);
  grayscaleBaseCaptured = true;
  return true;
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  if (frameBuffer && bwBuffer && frameBuffer != bwBuffer) {
    memcpy(frameBuffer, bwBuffer, BUFFER_SIZE);
  }
  grayscaleBaseCaptured = false;
}

void HalDisplay::displayGrayBuffer(HalDisplay::RefreshMode mode) {
  if (!displayReady || !gfx || !panelCanvas || !grayscaleLsbBuffer || !grayscaleMsbBuffer) {
    return;
  }

  if (!grayscaleBaseCaptured) {
    if (!captureGrayscaleBaseBuffer(frameBuffer)) {
      return;
    }
  } else if (!grayscaleBaseBuffer) {
    return;
  }

  renderGrayToPanelCanvas();

  if (forcedRefreshPending && (mode == FAST_REFRESH || mode == BALANCED_REFRESH)) {
    LOG_DBG("DSP", "Forcing requested refresh mode for next grayscale update");
    mode = forcedRefreshMode;
  }
  if (forceFullRefresh && mode != FULL_REFRESH) {
    LOG_DBG("DSP", "Forcing full refresh for first grayscale update");
    mode = FULL_REFRESH;
  }

  pushPanelCanvas(mode, epdModeForRefreshMode(mode));

  refreshCycleCount = 0;
  forcedRefreshPending = false;
  forceFullRefresh = false;
  pendingDisplayEffect = EFFECT_NONE;
  grayscaleBaseCaptured = false;
}

uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }

uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }

uint16_t HalDisplay::getVisibleWidth() const { return VISIBLE_WIDTH; }

uint16_t HalDisplay::getVisibleHeight() const { return VISIBLE_HEIGHT; }

uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }

uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

#endif  // BOARD_T5S3_PRO || BOARD_T5S3

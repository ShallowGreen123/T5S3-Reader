#include <HalDisplay.h>

#if defined(BOARD_LILYGO_EPD47_S3)

#include <Board.h>
#include <Logging.h>
#include <epd_driver.h>
#include <esp_heap_caps.h>

#include <cstring>

HalDisplay display;

namespace {
constexpr uint32_t kMiddleRefreshThreshold = 8;
constexpr uint32_t kQualityRefreshThreshold = 18;
constexpr size_t kPackedBufferSize =
    static_cast<size_t>(HalDisplay::DISPLAY_WIDTH) * HalDisplay::DISPLAY_HEIGHT / 2;

uint8_t* allocatePsramBuffer(const size_t size) {
  auto* buffer = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buffer) {
    buffer = static_cast<uint8_t*>(malloc(size));
  }
  return buffer;
}

Rect_t fullScreenRect() {
  return Rect_t{.x = 0,
                .y = 0,
                .width = static_cast<int32_t>(HalDisplay::DISPLAY_WIDTH),
                .height = static_cast<int32_t>(HalDisplay::DISPLAY_HEIGHT)};
}

void clearForMode(const HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      epd_clear();
      break;
    case HalDisplay::HALF_REFRESH:
      epd_clear_area_cycles(fullScreenRect(), 2, 45);
      break;
    case HalDisplay::BALANCED_REFRESH:
      epd_clear_area_cycles(fullScreenRect(), 1, 40);
      break;
    case HalDisplay::FAST_REFRESH:
    default:
      epd_clear_area_cycles(fullScreenRect(), 1, 30);
      break;
  }
}

uint8_t grayscaleNibble(const uint8_t baseByte, const uint8_t lsbByte, const uint8_t msbByte, const uint8_t mask) {
  if (baseByte & mask) {
    return 0x0F;
  }
  const bool lsb = (lsbByte & mask) != 0;
  const bool msb = (msbByte & mask) != 0;
  if (msb && !lsb) {
    return 0x0A;
  }
  if (lsb || msb) {
    return 0x05;
  }
  return 0x00;
}
}  // namespace

HalDisplay::HalDisplay() = default;

HalDisplay::~HalDisplay() {
  free(epdFrameBuffer);
  free(frameBuffer);
  free(grayscaleLsbBuffer);
  free(grayscaleMsbBuffer);
  free(grayscaleBaseBuffer);
}

uint8_t* HalDisplay::allocatePlane() {
  return allocatePsramBuffer(BUFFER_SIZE);
}

void HalDisplay::begin() {
  if (!frameBuffer) {
    frameBuffer = allocatePlane();
  }
  if (!epdFrameBuffer) {
    epdFrameBuffer = allocatePsramBuffer(kPackedBufferSize);
  }
  if (!frameBuffer || !epdFrameBuffer) {
    LOG_ERR("DSP", "Failed to allocate EPD47 framebuffers");
    return;
  }

  epd_init();
  clearScreen(0xFF);
  displayReady = true;
  forceFullRefresh = true;
  forcedRefreshPending = false;
  pendingDisplayEffect = EFFECT_NONE;
  refreshCycleCount = 0;
  grayscaleBaseCaptured = false;
  LOG_INF("DSP", "EPD47 display initialized: %ux%u visible, %ux%u scan", VISIBLE_WIDTH, VISIBLE_HEIGHT,
          DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void HalDisplay::clearScreen(const uint8_t color) const {
  if (frameBuffer) {
    memset(frameBuffer, color, BUFFER_SIZE);
  }
}

void HalDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                           const uint16_t h, const bool fromProgmem) const {
  if (!frameBuffer || !imageData || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }
  const uint16_t imageWidthBytes = w / 8;
  const uint16_t destByteX = x / 8;
  for (uint16_t row = 0; row < h; ++row) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) {
      break;
    }
    const uint32_t destOffset = static_cast<uint32_t>(destY) * DISPLAY_WIDTH_BYTES + destByteX;
    const uint32_t srcOffset = static_cast<uint32_t>(row) * imageWidthBytes;
    for (uint16_t col = 0; col < imageWidthBytes && destByteX + col < DISPLAY_WIDTH_BYTES; ++col) {
      frameBuffer[destOffset + col] =
          fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
    }
  }
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                                      const uint16_t h, const bool fromProgmem) const {
  if (!frameBuffer || !imageData || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }
  const uint16_t imageWidthBytes = w / 8;
  const uint16_t destByteX = x / 8;
  for (uint16_t row = 0; row < h; ++row) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) {
      break;
    }
    const uint32_t destOffset = static_cast<uint32_t>(destY) * DISPLAY_WIDTH_BYTES + destByteX;
    const uint32_t srcOffset = static_cast<uint32_t>(row) * imageWidthBytes;
    for (uint16_t col = 0; col < imageWidthBytes && destByteX + col < DISPLAY_WIDTH_BYTES; ++col) {
      const uint8_t source =
          fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
      frameBuffer[destOffset + col] &= source;
    }
  }
}

void HalDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen) {
  (void)turnOffScreen;
  if (!displayReady || !frameBuffer || !epdFrameBuffer) {
    return;
  }

  if (forcedRefreshPending && (mode == FAST_REFRESH || mode == BALANCED_REFRESH)) {
    mode = forcedRefreshMode;
  }
  if (forceFullRefresh && mode != FULL_REFRESH) {
    mode = FULL_REFRESH;
  }
  if (mode == FAST_REFRESH) {
    if (refreshCycleCount >= kQualityRefreshThreshold) {
      mode = FULL_REFRESH;
      refreshCycleCount = 0;
    } else if (refreshCycleCount >= kMiddleRefreshThreshold &&
               refreshCycleCount % kMiddleRefreshThreshold == 0) {
      mode = BALANCED_REFRESH;
    }
  }

  // The LilyGo 1-bit output path does not use DrawMode_t and produces a
  // different scan ordering from the supported 4-bit path. Expand the logical
  // 1=white, 0=black framebuffer to pure 4-bit white/black pixels instead.
  size_t packedIndex = 0;
  for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
    const uint8_t source = frameBuffer[i];
    for (uint8_t bit = 0; bit < 8; bit += 2) {
      const uint8_t left = (source & (0x80 >> bit)) ? 0x0F : 0x00;
      const uint8_t right = (source & (0x40 >> bit)) ? 0x0F : 0x00;
      epdFrameBuffer[packedIndex++] = static_cast<uint8_t>(left | (right << 4));
    }
  }

  epd_poweron();
  clearForMode(mode);
  epd_draw_grayscale_image(fullScreenRect(), epdFrameBuffer);
  epd_poweroff_all();

  refreshCycleCount = mode == FAST_REFRESH ? refreshCycleCount + 1 : 0;
  forcedRefreshPending = false;
  forceFullRefresh = false;
  pendingDisplayEffect = EFFECT_NONE;
}

void HalDisplay::refreshDisplay(const RefreshMode mode, const bool turnOffScreen) { displayBuffer(mode, turnOffScreen); }

void HalDisplay::requestNextRefresh(const RefreshMode mode) {
  forcedRefreshMode = mode;
  forcedRefreshPending = true;
}

void HalDisplay::requestNextDisplayEffect(const DisplayEffect effect) { pendingDisplayEffect = effect; }

void HalDisplay::suppressInitialFullRefresh() { forceFullRefresh = false; }

void HalDisplay::deepSleep() {
  epd_poweroff_all();
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

void HalDisplay::displayGrayBuffer(RefreshMode mode) {
  if (!displayReady || !grayscaleLsbBuffer || !grayscaleMsbBuffer) {
    return;
  }
  if (!grayscaleBaseCaptured && !captureGrayscaleBaseBuffer(frameBuffer)) {
    return;
  }

  if (!epdFrameBuffer) {
    return;
  }

  size_t packedIndex = 0;
  for (uint32_t byteIndex = 0; byteIndex < BUFFER_SIZE; ++byteIndex) {
    const uint8_t base = grayscaleBaseBuffer[byteIndex];
    const uint8_t lsb = grayscaleLsbBuffer[byteIndex];
    const uint8_t msb = grayscaleMsbBuffer[byteIndex];
    for (uint8_t bit = 0; bit < 8; bit += 2) {
      const uint8_t left = grayscaleNibble(base, lsb, msb, static_cast<uint8_t>(0x80 >> bit));
      const uint8_t right = grayscaleNibble(base, lsb, msb, static_cast<uint8_t>(0x40 >> bit));
      // LilyGo-EPD47 stores even X in the low nibble and odd X in the high nibble.
      epdFrameBuffer[packedIndex++] = static_cast<uint8_t>(left | (right << 4));
    }
  }

  if (forcedRefreshPending && (mode == FAST_REFRESH || mode == BALANCED_REFRESH)) {
    mode = forcedRefreshMode;
  }
  if (forceFullRefresh && mode != FULL_REFRESH) {
    mode = FULL_REFRESH;
  }
  epd_poweron();
  clearForMode(mode);
  epd_draw_grayscale_image(fullScreenRect(), epdFrameBuffer);
  epd_poweroff_all();

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

#endif  // BOARD_LILYGO_EPD47_S3

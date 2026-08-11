#pragma once
#include <Arduino.h>
#include <Board.h>

#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
class T5S3M5GfxDisplay;

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
namespace epd_mode {
enum epd_mode_t : uint8_t;
}
}
}  // namespace lgfx
#endif

class HalDisplay {
 public:
  // Constructor with pin configuration
  HalDisplay();

  // Destructor
  ~HalDisplay();

  // Refresh modes
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    BALANCED_REFRESH,  // Reader-focused fast refresh using the cleaner mid waveform
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  enum DisplayEffect {
    EFFECT_NONE,
    EFFECT_READER_TURN_FORWARD_STANDARD,
    EFFECT_READER_TURN_BACKWARD_STANDARD,
    EFFECT_READER_TURN_FORWARD_FAST,
    EFFECT_READER_TURN_BACKWARD_FAST
  };

  // Initialize the display hardware and driver
  void begin();

  // Display dimensions
  static constexpr uint16_t VISIBLE_WIDTH = BoardPins::LogicalWidth;
  static constexpr uint16_t VISIBLE_HEIGHT = BoardPins::LogicalHeight;
  // Keep the framebuffer in physical 960x540 scan orientation while exposing
  // portrait logical coordinates to the UI.
  static constexpr uint16_t DISPLAY_WIDTH = ((BoardPins::DisplayWidth + 15) / 16) * 16;
  static constexpr uint16_t DISPLAY_HEIGHT = BoardPins::DisplayHeight;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  void requestNextRefresh(RefreshMode mode = RefreshMode::HALF_REFRESH);
  void requestNextDisplayEffect(DisplayEffect effect = DisplayEffect::EFFECT_NONE);
  void suppressInitialFullRefresh();

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  bool captureGrayscaleBaseBuffer(const uint8_t* bwBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer(RefreshMode mode = RefreshMode::HALF_REFRESH);

  // Runtime geometry passthrough
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getVisibleWidth() const;
  uint16_t getVisibleHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

 private:
#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
  T5S3M5GfxDisplay* gfx = nullptr;
  lgfx::LGFX_Sprite* panelCanvas = nullptr;
#elif defined(BOARD_LILYGO_EPD47_S3)
  uint8_t* epdFrameBuffer = nullptr;
#endif
  uint8_t* frameBuffer = nullptr;
  uint8_t* grayscaleLsbBuffer = nullptr;
  uint8_t* grayscaleMsbBuffer = nullptr;
  uint8_t* grayscaleBaseBuffer = nullptr;
  bool grayscaleBaseCaptured = false;
  bool displayReady = false;
  bool forceFullRefresh = true;
  bool forcedRefreshPending = false;
  RefreshMode forcedRefreshMode = RefreshMode::HALF_REFRESH;
  DisplayEffect pendingDisplayEffect = DisplayEffect::EFFECT_NONE;
  uint32_t refreshCycleCount = 0;

  uint8_t* allocatePlane();
#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
  void releaseBackend();
  bool initializePanelCanvas();
  void pushPanelCanvas(RefreshMode mode, lgfx::epd_mode::epd_mode_t epdMode);
  void pushPanelCanvasWithEffect(DisplayEffect effect) const;
  void renderBwToPanelCanvas() const;
  void renderGrayToPanelCanvas() const;
#endif
};

extern HalDisplay display;

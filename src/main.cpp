#include <Arduino.h>
#include <BoardT5S3.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <builtinFonts/all.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"

MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

// Fonts
EpdFont notoserif14RegularFont(&notoserif_14_regular);
EpdFont notoserif14BoldFont(&notoserif_14_bold);
EpdFont notoserif14ItalicFont(&notoserif_14_italic);
EpdFont notoserif14BoldItalicFont(&notoserif_14_bolditalic);
EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                    &notoserif14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont notoserif12RegularFont(&notoserif_12_regular);
EpdFont notoserif12BoldFont(&notoserif_12_bold);
EpdFont notoserif12ItalicFont(&notoserif_12_italic);
EpdFont notoserif12BoldItalicFont(&notoserif_12_bolditalic);
EpdFontFamily notoserif12FontFamily(&notoserif12RegularFont, &notoserif12BoldFont, &notoserif12ItalicFont,
                                    &notoserif12BoldItalicFont);
EpdFont notoserif16RegularFont(&notoserif_16_regular);
EpdFont notoserif16BoldFont(&notoserif_16_bold);
EpdFont notoserif16ItalicFont(&notoserif_16_italic);
EpdFont notoserif16BoldItalicFont(&notoserif_16_bolditalic);
EpdFontFamily notoserif16FontFamily(&notoserif16RegularFont, &notoserif16BoldFont, &notoserif16ItalicFont,
                                    &notoserif16BoldItalicFont);
EpdFont notoserif18RegularFont(&notoserif_18_regular);
EpdFont notoserif18BoldFont(&notoserif_18_bold);
EpdFont notoserif18ItalicFont(&notoserif_18_italic);
EpdFont notoserif18BoldItalicFont(&notoserif_18_bolditalic);
EpdFontFamily notoserif18FontFamily(&notoserif18RegularFont, &notoserif18BoldFont, &notoserif18ItalicFont,
                                    &notoserif18BoldItalicFont);

EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);

EpdFont opendyslexic8RegularFont(&opendyslexic_8_regular);
EpdFont opendyslexic8BoldFont(&opendyslexic_8_bold);
EpdFont opendyslexic8ItalicFont(&opendyslexic_8_italic);
EpdFont opendyslexic8BoldItalicFont(&opendyslexic_8_bolditalic);
EpdFontFamily opendyslexic8FontFamily(&opendyslexic8RegularFont, &opendyslexic8BoldFont, &opendyslexic8ItalicFont,
                                      &opendyslexic8BoldItalicFont);
EpdFont opendyslexic10RegularFont(&opendyslexic_10_regular);
EpdFont opendyslexic10BoldFont(&opendyslexic_10_bold);
EpdFont opendyslexic10ItalicFont(&opendyslexic_10_italic);
EpdFont opendyslexic10BoldItalicFont(&opendyslexic_10_bolditalic);
EpdFontFamily opendyslexic10FontFamily(&opendyslexic10RegularFont, &opendyslexic10BoldFont, &opendyslexic10ItalicFont,
                                       &opendyslexic10BoldItalicFont);
EpdFont opendyslexic12RegularFont(&opendyslexic_12_regular);
EpdFont opendyslexic12BoldFont(&opendyslexic_12_bold);
EpdFont opendyslexic12ItalicFont(&opendyslexic_12_italic);
EpdFont opendyslexic12BoldItalicFont(&opendyslexic_12_bolditalic);
EpdFontFamily opendyslexic12FontFamily(&opendyslexic12RegularFont, &opendyslexic12BoldFont, &opendyslexic12ItalicFont,
                                       &opendyslexic12BoldItalicFont);
EpdFont opendyslexic14RegularFont(&opendyslexic_14_regular);
EpdFont opendyslexic14BoldFont(&opendyslexic_14_bold);
EpdFont opendyslexic14ItalicFont(&opendyslexic_14_italic);
EpdFont opendyslexic14BoldItalicFont(&opendyslexic_14_bolditalic);
EpdFontFamily opendyslexic14FontFamily(&opendyslexic14RegularFont, &opendyslexic14BoldFont, &opendyslexic14ItalicFont,
                                       &opendyslexic14BoldItalicFont);
#endif  // OMIT_FONTS

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;
constexpr unsigned long kBootConfirmHoldMs = 700;
constexpr unsigned long kPcaButtonPowerOffHoldMs = 2000;

void renderPowerOffScreen(const char* status) {
  RenderLock lock;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2 - 30, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 40, "CrossPoint", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 72, status, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 102, "Hold PWR to power on");
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

// Verify power button press duration on wake-up from deep sleep
// Pre-condition: isWakeupByPowerButton() == true
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    // Fast path for short press
    // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // Subtract the current time, because inputManager only starts counting the HeldTime from the first update()
  // This way, we remove the time we already took to reach here from the duration,
  // assuming the button was held until now from millis()==0 (i.e. device start time).
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < calibratedPressDuration);
    abort = gpio.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    powerManager.startDeepSleep(gpio);
  }
}
void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

bool shouldSuppressDeepSleepForDebug() {
#ifdef ENABLE_SERIAL_LOG
  return Serial;
#else
  return false;
#endif
}

// Enter deep sleep mode
void enterDeepSleep() {
  if (shouldSuppressDeepSleepForDebug()) {
    LOG_DBG("MAIN", "Deep sleep suppressed while serial is connected");
    waitForPowerRelease();
    return;
  }

  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();
  APP_STATE.saveToFile();

  activityManager.goToSleep();
  BoardT5S3::setBacklightLevel(0);

  halTiltSensor.deepSleep();
  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

void enterDeepSleepKeepingScreen(bool wakeOnTouch = true) {
  HalPowerManager::Lock powerLock;
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();
  APP_STATE.saveToFile();

  BoardT5S3::setBacklightLevel(0);
  halTiltSensor.deepSleep();
  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep with current screen preserved, wakeOnTouch=%d", wakeOnTouch ? 1 : 0);

  powerManager.startDeepSleep(gpio, wakeOnTouch);
}

void enterPowerOffKeepingScreen(const char* status) {
  {
    HalPowerManager::Lock powerLock;
    APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();
    APP_STATE.saveToFile();
    display.deepSleep();

    renderPowerOffScreen(status);
    BoardT5S3::setBacklightLevel(0);
    if (BoardT5S3::shutdownBatteryPower()) {
      delay(1500);
      LOG_DBG("MAIN", "BQ25896 shutdown returned but device is still running; falling back to deep sleep");
    } else {
      renderPowerOffScreen("Entering sleep mode...");
      BoardT5S3::setBacklightLevel(0);
      LOG_ERR("MAIN", "BQ25896 shutdown failed or rejected; falling back to deep sleep");
    }
  }

  enterDeepSleepKeepingScreen(false);
}

void setupDisplayAndFonts() {
  display.begin();
  renderer.begin();
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12FontFamily);
  renderer.insertFont(NOTOSERIF_16_FONT_ID, notoserif16FontFamily);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18FontFamily);

  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
  renderer.insertFont(OPENDYSLEXIC_8_FONT_ID, opendyslexic8FontFamily);
  renderer.insertFont(OPENDYSLEXIC_10_FONT_ID, opendyslexic10FontFamily);
  renderer.insertFont(OPENDYSLEXIC_12_FONT_ID, opendyslexic12FontFamily);
  renderer.insertFont(OPENDYSLEXIC_14_FONT_ID, opendyslexic14FontFamily);
#endif  // OMIT_FONTS
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

  sdFontSystem.begin(renderer);
  LOG_DBG("MAIN", "Fonts setup");
}

void ensureSdFontLoaded() { sdFontSystem.ensureLoaded(renderer); }

HalDisplay::RefreshMode readerResumeRefreshMode() {
  switch (SETTINGS.readerDisplayMode) {
    case CrossPointSettings::READER_DISPLAY_FAST:
      return HalDisplay::FAST_REFRESH;
    case CrossPointSettings::READER_DISPLAY_STANDARD:
      return HalDisplay::BALANCED_REFRESH;
    case CrossPointSettings::READER_DISPLAY_QUALITY:
    default:
      return HalDisplay::HALF_REFRESH;
  }
}

bool shouldResumeReaderOnBoot() {
  return SETTINGS.resumeReaderOnBoot && !APP_STATE.openEpubPath.empty() && APP_STATE.lastSleepFromReader &&
         !mappedInputManager.isPressed(MappedInputManager::Button::Back) && APP_STATE.readerActivityLoadCount == 0;
}

void setup() {
  t1 = millis();

  HalSystem::begin();
  gpio.begin();
  powerManager.begin();
  halClock.begin();
  halTiltSensor.begin();

#ifdef ENABLE_SERIAL_LOG
  Serial.begin(115200);
  const unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart) < 500) {
    delay(10);
  }
#endif

  LOG_INF("MAIN", "Hardware detect: %s", gpio.getDeviceName());
  LOG_INF("MAIN", "Touch detect: %d", gpio.isTouchAvailable());

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts();
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  HalSystem::checkPanic();

  SETTINGS.loadFromFile();
  LOG_DBG("MAIN", "Clock settings: tz=%u rtcStoresUtc=%u rtcVariantHint=%u rtcReferenceEpoch=%lu",
          static_cast<unsigned>(SETTINGS.timeZone), static_cast<unsigned>(SETTINGS.rtcStoresUtc),
          static_cast<unsigned>(SETTINGS.rtcVariantHint), static_cast<unsigned long>(SETTINGS.rtcReferenceEpoch));
  halClock.configure(SETTINGS.timeZone, SETTINGS.rtcStoresUtc != 0, SETTINGS.rtcVariantHint, SETTINGS.rtcReferenceEpoch);
  if (!halClock.syncSystemTimeFromRtc()) {
    LOG_DBG("MAIN", "RTC time unavailable or invalid at boot");
  } else if (SETTINGS.rtcStoresUtc != static_cast<uint8_t>(halClock.getRtcStoresUtc())) {
    SETTINGS.rtcStoresUtc = halClock.getRtcStoresUtc() ? 1 : 0;
    if (SETTINGS.saveToFile()) {
      LOG_DBG("MAIN", "Updated RTC storage mode setting after boot auto-correction");
    } else {
      LOG_ERR("MAIN", "Failed to persist RTC storage mode auto-correction");
    }
  }
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  const auto wakeupReason = gpio.getWakeupReason();
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      LOG_DBG("MAIN", "Verifying power button wakeup");
      gpio.verifyPowerButtonWakeup(10, true);
      break;
    case HalGPIO::WakeupReason::Touch:
      LOG_DBG("MAIN", "Wakeup reason: Touch");
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  BoardT5S3::setBacklightLevel(SETTINGS.backlightLevel);

  // Recovery firmware mode: hold left side button (BTN_UP) together with the power button at
  // boot to skip directly to the SD-card firmware update screen. Useful on devices where USB
  // flashing has been locked down (e.g. recent X3 firmware).
  bool recoveryFirmwareMode = false;
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    // Refresh the cached button state a few times — isPressed() needs ~half a second to settle
    // after boot per the HalGPIO contract. Use a millis-based deadline so we always wait the full
    // settle window even if the loop body takes longer than expected on slow boots.
    const unsigned long settleStart = millis();
    while (millis() - settleStart < 500) {
      gpio.update();
      delay(10);
    }
    if (gpio.isPressed(HalGPIO::BTN_UP)) {
      recoveryFirmwareMode = true;
      LOG_INF("MAIN", "Recovery firmware mode (UP + POWER held at boot)");
    }
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  setupDisplayAndFonts();

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  const bool resumeReaderOnBoot = shouldResumeReaderOnBoot();
  const auto prepareStartupRefresh = [](HalDisplay::RefreshMode refreshMode) {
    display.suppressInitialFullRefresh();
    renderer.requestNextRefresh(refreshMode);
  };

  if (recoveryFirmwareMode) {
    prepareStartupRefresh(HalDisplay::HALF_REFRESH);
    // Skip the boot splash and jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true),
        HalDisplay::HALF_REFRESH);
  } else if (HalSystem::isRebootFromPanic()) {
    prepareStartupRefresh(HalDisplay::HALF_REFRESH);
    // If we rebooted from a panic, go directly to the crash report screen to show the panic info.
    activityManager.goToCrashReport();
  } else if (!resumeReaderOnBoot) {
    prepareStartupRefresh(HalDisplay::HALF_REFRESH);
    // Boot directly to home screen to avoid a full-refresh boot splash before the main page.
    activityManager.goHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    display.suppressInitialFullRefresh();
    activityManager.goToReader(path, readerResumeRefreshMode());
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.update();
  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.hadTouchActivity() || halTiltSensor.hadActivity() ||
      activityManager.preventAutoSleep()
#ifdef ENABLE_SERIAL_LOG
      || Serial
#endif
  ) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  static bool screenshotButtonsReleased = true;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  } else {
    screenshotButtonsReleased = true;
  }

  bool hasHardwareButtonTap = false;
  auto hardwareButtonTap = MappedInputManager::Button::Up;
  auto queueHardwareButtonTap = [&](const MappedInputManager::Button button) {
    if (!hasHardwareButtonTap) {
      hardwareButtonTap = button;
      hasHardwareButtonTap = true;
    }
  };
  const bool isReaderPage = activityManager.isReaderPageActivity();

  static bool bootLongConfirmHandled = false;
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    if (!bootLongConfirmHandled && gpio.getHeldTime() >= kBootConfirmHoldMs) {
      LOG_DBG("MAIN", "BOOT long press mapped to Confirm");
      bootLongConfirmHandled = true;
      queueHardwareButtonTap(MappedInputManager::Button::Confirm);
    }
  } else {
    if (gpio.wasReleased(HalGPIO::BTN_POWER) && !bootLongConfirmHandled) {
      LOG_DBG("MAIN", "BOOT short press mapped to %s", isReaderPage ? "PageBack" : "Up");
      queueHardwareButtonTap(isReaderPage ? MappedInputManager::Button::PageBack : MappedInputManager::Button::Up);
    }
    bootLongConfirmHandled = false;
  }

  static bool pcaPowerOffHandled = false;
  if (!gpio.isPressed(HalGPIO::BTN_PCA)) {
    if (gpio.wasReleased(HalGPIO::BTN_PCA) && !pcaPowerOffHandled) {
      LOG_DBG("MAIN", "PCA9535 button short press mapped to %s", isReaderPage ? "PageForward" : "Down");
      queueHardwareButtonTap(isReaderPage ? MappedInputManager::Button::PageForward : MappedInputManager::Button::Down);
    }
    pcaPowerOffHandled = false;
  } else if (!pcaPowerOffHandled && gpio.getHeldTime() >= kPcaButtonPowerOffHoldMs) {
    LOG_DBG("MAIN", "PCA9535 button long press BQ25896 shutdown request");
    pcaPowerOffHandled = true;
    enterPowerOffKeepingScreen("Shutting down...");
    return;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    if (gpio.isUsbConnected()) {
      LOG_DBG("SLP", "Auto power-off skipped after %lu ms of inactivity because USB is connected", sleepTimeoutMs);
      lastActivityTime = millis();
    } else {
      LOG_DBG("SLP", "Auto power-off triggered after %lu ms of inactivity", sleepTimeoutMs);
      enterPowerOffKeepingScreen("Shutting down...");
      // This should never be hit as the fallback path calls esp_deep_sleep_start
      return;
    }
  }

  // if (millis() > 5000 && gpio.isPressed(HalGPIO::BTN_POWER) &&
  //     gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
  //   // If the screenshot combination is potentially being pressed, don't sleep
  //   if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
  //     return;
  //   }
  //   LOG_DBG("MAIN", "Power button sleep request, held=%lu ms", gpio.getHeldTime());
  //   enterDeepSleep();
  //   // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
  //   return;
  // }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  if (hasHardwareButtonTap) {
    mappedInputManager.injectButtonTap(hardwareButtonTap);
  }
  activityManager.loop();
  if (hasHardwareButtonTap) {
    mappedInputManager.clearInjectedButtonTap();
  }
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}

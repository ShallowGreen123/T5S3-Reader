#pragma once

#include <HalTiltSensor.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>
#include <Board.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "activities/settings/SettingsActivity.h"

inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  SettingInfo setting =
      SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                        {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER);

  if (registry == nullptr || registry->getFamilyCount() == 0) {
    return setting;
  }

  std::vector<std::string> sdFamilyNames;
  const auto& families = registry->getFamilies();
  sdFamilyNames.reserve(families.size());
  std::transform(families.begin(), families.end(), std::back_inserter(sdFamilyNames),
                 [](const SdCardFontFamilyInfo& family) { return family.name; });

  setting.enumStringValues.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + sdFamilyNames.size());
  setting.enumStringValues.push_back(I18N.get(StrId::STR_NOTO_SERIF));
  setting.enumStringValues.push_back(I18N.get(StrId::STR_NOTO_SANS));
  setting.enumStringValues.insert(setting.enumStringValues.end(), sdFamilyNames.begin(), sdFamilyNames.end());
  setting.valuePtr = nullptr;

  setting.valueGetter = [sdFamilyNames]() -> uint8_t {
    if (SETTINGS.sdFontFamilyName[0] != '\0') {
      for (size_t i = 0; i < sdFamilyNames.size(); i++) {
        if (sdFamilyNames[i] == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
        }
      }
    }
    return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily
                                                                        : CrossPointSettings::NOTOSERIF;
  };

  setting.valueSetter = [sdFamilyNames](uint8_t value) {
    if (value < CrossPointSettings::BUILTIN_FONT_COUNT) {
      SETTINGS.fontFamily = value;
      SETTINGS.sdFontFamilyName[0] = '\0';
      return;
    }

    const int sdIndex = value - CrossPointSettings::BUILTIN_FONT_COUNT;
    if (sdIndex >= 0 && sdIndex < static_cast<int>(sdFamilyNames.size())) {
      strncpy(SETTINGS.sdFontFamilyName, sdFamilyNames[sdIndex].c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      SETTINGS.fontFamily = CrossPointSettings::NOTOSERIF;
    }
  };

  return setting;
}

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr) {
  static const std::vector<SettingInfo> baseList = [] {
    std::vector<SettingInfo> v = {
        // --- Display ---
        SettingInfo::Value(StrId::STR_BACKLIGHT, &CrossPointSettings::backlightLevel, {0, 10, 1}, "backlightLevel",
                           StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen,
                          {StrId::STR_DARK, StrId::STR_LIGHT, StrId::STR_CUSTOM, StrId::STR_COVER, StrId::STR_NONE_OPT,
                           StrId::STR_COVER_CUSTOM},
                          "sleepScreen", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_POWER_OFF_SCREEN, &CrossPointSettings::powerOffScreen,
                          {StrId::STR_DARK, StrId::STR_LIGHT, StrId::STR_CUSTOM, StrId::STR_COVER, StrId::STR_NONE_OPT,
                           StrId::STR_COVER_CUSTOM},
                          "powerOffScreen", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                          {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                          {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                          "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_POWER_OFF_COVER_MODE, &CrossPointSettings::powerOffScreenCoverMode,
                          {StrId::STR_FIT, StrId::STR_CROP}, "powerOffScreenCoverMode", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_POWER_OFF_COVER_FILTER, &CrossPointSettings::powerOffScreenCoverFilter,
                          {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                          "powerOffScreenCoverFilter", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                          {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(
            StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
            {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30},
            "refreshFrequency", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme,
                          {StrId::STR_THEME_CLASSIC, StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED,
                           StrId::STR_THEME_ROUNDEDRAFF},
                          "uiTheme", StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                            StrId::STR_CAT_DISPLAY),

        // --- Reader ---
        SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                          {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
                          {StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE}, "fontSize",
                          StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                          {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE}, "lineSpacing", StrId::STR_CAT_READER),
        SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin, {5, 40, 5}, "screenMargin",
                           StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                          {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                           StrId::STR_BOOK_S_STYLE},
                          "paragraphAlignment", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                            StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled",
                            StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                          {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW},
                          "orientation", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                            "extraParagraphSpacing", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                            StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_READER_DISPLAY_MODE, &CrossPointSettings::readerDisplayMode,
                          {StrId::STR_DISPLAY_QUALITY, StrId::STR_DISPLAY_STANDARD, StrId::STR_DISPLAY_FAST},
                          "readerDisplayMode", StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                          {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                          "imageRendering", StrId::STR_CAT_READER),

        // --- Controls ---
        SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                          {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV}, "sideButtonLayout", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_MENU, &CrossPointSettings::longPressMenuFunction,
                          {StrId::STR_KOREADER_SYNC, StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION},
                          "longPressMenuFunction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                          {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                           StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                          "longPressButtonBehavior", StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_DOUBLE_CLICK_HOME, &CrossPointSettings::doubleClickHomeMenu,
                            "doubleClickHomeMenu", StrId::STR_CAT_CONTROLS),

        // --- System ---
        SettingInfo::Enum(StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeout,
                          {StrId::STR_MIN_1, StrId::STR_MIN_5, StrId::STR_MIN_10, StrId::STR_MIN_15, StrId::STR_MIN_30},
                          "sleepTimeout", StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_HIDE_CLOCK, &CrossPointSettings::hideClock, "hideClock",
                            StrId::STR_CAT_SYSTEM),
        SettingInfo::TimeZone(StrId::STR_TIME_ZONE, SETTINGS.timeZoneId, sizeof(SETTINGS.timeZoneId), "timeZone",
                              StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, "showHiddenFiles",
                            StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_AUTO_REMOVE_FINISHED_BOOKS,
                            &CrossPointSettings::autoRemoveFinishedRecentBooks, "autoRemoveFinishedRecentBooks",
                            StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_CONFIRM_SHUTDOWN, &CrossPointSettings::confirmShutdown, "confirmShutdown",
                            StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_FLIP_UI, &CrossPointSettings::flipUi, "flipUi", StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_RESUME_ON_BOOT, &CrossPointSettings::resumeReaderOnBoot, "resumeReaderOnBoot",
                            StrId::STR_CAT_SYSTEM),

        // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
        SettingInfo::DynamicString(
            StrId::STR_KOREADER_USERNAME, [] { return KOREADER_STORE.getUsername(); },
            [](const std::string& value) {
              KOREADER_STORE.setCredentials(value, KOREADER_STORE.getPassword());
              KOREADER_STORE.saveToFile();
            },
            "koUsername", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicString(
            StrId::STR_KOREADER_PASSWORD, [] { return KOREADER_STORE.getPassword(); },
            [](const std::string& value) {
              KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), value);
              KOREADER_STORE.saveToFile();
            },
            "koPassword", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicString(
            StrId::STR_SYNC_SERVER_URL, [] { return KOREADER_STORE.getServerUrl(); },
            [](const std::string& value) {
              KOREADER_STORE.setServerUrl(value);
              KOREADER_STORE.saveToFile();
            },
            "koServerUrl", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
            [](uint8_t value) {
              KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(value));
              KOREADER_STORE.saveToFile();
            },
            "koMatchMethod", StrId::STR_KOREADER_SYNC),

        // --- Status Bar Settings (web-only, uses StatusBarSettingsActivity) ---
        SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                            "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &CrossPointSettings::statusBarBookProgressPercentage,
                            "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarProgressBarThickness,
                          {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                          "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock,
                          {StrId::STR_HIDE, StrId::STR_DIR_LEFT, StrId::STR_DIR_RIGHT}, "statusBarClock",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                            StrId::STR_CUSTOMISE_STATUS_BAR),
    };

    if (!Board::capabilities().hasBacklight) {
      v.erase(std::remove_if(v.begin(), v.end(), [](const SettingInfo& setting) {
                return setting.nameId == StrId::STR_BACKLIGHT;
              }),
              v.end());
    }

    if (halTiltSensor.isAvailable()) {
      for (auto it = v.begin(); it != v.end(); ++it) {
        if (it->nameId == StrId::STR_LONG_PRESS_BEHAVIOR) {
          v.insert(it + 1, SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                                             {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED},
                                             "tiltPageTurn", StrId::STR_CAT_CONTROLS));
          break;
        }
      }
    }
    return v;
  }();

  std::vector<SettingInfo> list = baseList;
  auto it = std::find_if(list.begin(), list.end(),
                         [](const SettingInfo& setting) { return setting.nameId == StrId::STR_FONT_FAMILY; });
  if (it != list.end()) {
    *it = buildFontFamilySetting(registry);
  }
  return list;
}

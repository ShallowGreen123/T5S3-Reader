#include "FileBrowserActivity.h"

#include <Epub.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "../util/ConfirmationActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr char UTF8_ELLIPSIS[] = "\xE2\x80\xA6";

void appendTextKey(std::string& key, const std::string& text) {
  if (text.empty()) {
    return;
  }
  key.push_back('\n');
  key += text;
}

void recordRoleText(FontCacheManager* fcm, const int systemFontId, const TextRole role, const char* text,
                    const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (fcm == nullptr || text == nullptr || text[0] == '\0') {
    return;
  }
  fcm->recordText(text, BaseTheme::resolveTextFontId(systemFontId, role), style);
}
}  // namespace

void FileBrowserActivity::loadFiles() {
  files.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    return;
  }

  root.rewindDirectory();

  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if ((!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0) {
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(std::string(name) + "/");
    } else {
      std::string_view filename{name};
      if (mode == Mode::PickFirmware) {
        // Firmware picker: only show .bin files.
        if (FsHelpers::checkFileExtension(filename, ".bin")) {
          files.emplace_back(filename);
        }
      } else if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                 FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
                 FsHelpers::hasBmpExtension(filename)) {
        files.emplace_back(filename);
      }
    }
  }
  root.close();
  FsHelpers::sortFileList(files);
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();

  selectorIndex = 0;
  lastVisibleTextPrewarmKey.clear();

  // If Confirm was held while this activity opened (typical when launched from a menu), ignore
  // its release — otherwise we'd immediately auto-open whatever is at index 0.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    selectorIndex = findEntry(fileName);
  } else {
    loadFiles();
  }

  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
  lastVisibleTextPrewarmKey.clear();
}

void FileBrowserActivity::clearFileMetadata(const std::string& fullPath) {
  // Only clear cache for .epub files
  if (FsHelpers::hasEpubExtension(fullPath)) {
    Epub(fullPath, "/.crosspoint").clearCache();
    LOG_DBG("FileBrowser", "Cleared metadata cache for: %s", fullPath.c_str());
  }
}

void FileBrowserActivity::openSelectedEntry() {
  if (files.empty() || selectorIndex >= files.size()) return;

  const std::string& entry = files[selectorIndex];
  const bool isDirectory = (entry.back() == '/');

  // Firmware picker: select file -> return path; navigate into directories normally.
  if (mode == Mode::PickFirmware && !isDirectory) {
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    ActivityResult res{FilePathResult{cleanBasePath + entry}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  if (basepath.back() != '/') basepath += "/";

  if (isDirectory) {
    basepath += entry.substr(0, entry.length() - 1);
    loadFiles();
    selectorIndex = 0;
    requestUpdate();
  } else {
    onSelectBook(basepath + entry);
  }
}

void FileBrowserActivity::confirmDeleteSelectedFile() {
  if (files.empty() || selectorIndex >= files.size()) return;

  const std::string& entry = files[selectorIndex];
  const bool isDirectory = (entry.back() == '/');
  if (isDirectory) return;

  std::string cleanBasePath = basepath;
  if (cleanBasePath.back() != '/') cleanBasePath += "/";
  const std::string fullPath = cleanBasePath + entry;

  auto handler = [this, fullPath](const ActivityResult& res) {
    if (!res.isCancelled) {
      LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
      clearFileMetadata(fullPath);
      if (Storage.remove(fullPath.c_str())) {
        LOG_DBG("FileBrowser", "Deleted successfully");
        loadFiles();
        if (files.empty()) {
          selectorIndex = 0;
        } else if (selectorIndex >= files.size()) {
          // Move selection to the new "last" item
          selectorIndex = files.size() - 1;
        }

        requestUpdate(true);
      } else {
        LOG_ERR("FileBrowser", "Failed to delete file: %s", fullPath.c_str());
      }
    } else {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
    }
  };

  std::string heading = tr(STR_DELETE) + std::string("? ");

  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry), handler);
}

void FileBrowserActivity::loop() {
  // Long press BACK (1s+) goes to root folder (Books mode only).
  // In firmware-pick mode we keep navigation simple: short Back = up dir / cancel.
  if (mode == Mode::Books && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= GO_HOME_MS && basepath != "/" && !lockLongPressBack) {
    basepath = "/";
    loadFiles();
    selectorIndex = 0;
    requestUpdate();
    return;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return;
  }

  const int pathReserved =
      BaseTheme::getLineHeightForRole(renderer, SMALL_FONT_ID, TextRole::UserContent) +
      UITheme::getInstance().getMetrics().verticalSpacing;
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmPressedAt = millis();
    confirmPressStarted = true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const unsigned long confirmHeldTime =
        confirmPressStarted ? millis() - confirmPressedAt : mappedInput.getHeldTime();
    confirmPressStarted = false;

    if (lockNextConfirmRelease) {
      lockNextConfirmRelease = false;
      return;
    }
    if (files.empty()) return;

    const bool isDirectory = (files[selectorIndex].back() == '/');
    if (mode == Mode::Books && confirmHeldTime >= GO_HOME_MS && !isDirectory) {
      // --- LONG PRESS ACTION: DELETE FILE ---
      confirmDeleteSelectedFile();
      return;
    } else {
      // --- SHORT PRESS ACTION: OPEN/NAVIGATE ---
      openSelectedEntry();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);

        requestUpdate();
      } else if (mode == Mode::PickFirmware) {
        // Firmware picker at root: cancel back to caller instead of going home.
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        onGoHome();
      }
    }
  }

  int listSize = static_cast<int>(files.size());
  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

bool FileBrowserActivity::onTouchTap(int16_t, int16_t y) {
  if (files.empty()) return false;

  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pathLineHeight = BaseTheme::getLineHeightForRole(renderer, SMALL_FONT_ID, TextRole::UserContent);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  const int rowHeight = metrics.listRowHeight;
  if (rowHeight <= 0 || y < contentTop || y >= contentTop + contentHeight) {
    return false;
  }

  const int pageItems = std::max(1, contentHeight / rowHeight);
  const int row = (y - contentTop) / rowHeight;
  if (row < 0 || row >= pageItems) {
    return false;
  }

  const size_t pageStartIndex = (selectorIndex / pageItems) * pageItems;
  const size_t touchedIndex = pageStartIndex + static_cast<size_t>(row);
  if (touchedIndex >= files.size()) {
    return false;
  }

  selectorIndex = touchedIndex;
  confirmPressStarted = false;
  openSelectedEntry();
  return true;
}

bool FileBrowserActivity::onTouchSwipe(int16_t, int16_t startY, int16_t, int16_t endY) {
  constexpr int kVerticalSwipeMinimum = 70;
  const int verticalDistance = static_cast<int>(endY) - startY;
  if (std::abs(verticalDistance) < kVerticalSwipeMinimum || files.empty()) {
    return false;
  }

  const int pathReserved =
      BaseTheme::getLineHeightForRole(renderer, SMALL_FONT_ID, TextRole::UserContent) +
      UITheme::getInstance().getMetrics().verticalSpacing;
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);
  const int listSize = static_cast<int>(files.size());
  selectorIndex = verticalDistance < 0
                      ? ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems)
                      : ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
  requestUpdate();
  return true;
}

std::string getFileName(std::string filename) {
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

std::string getFileExtension(std::string filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

void FileBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName =
      (mode == Mode::PickFirmware)
          ? std::string(tr(STR_SELECT_FIRMWARE_FILE))
          : ((basepath == "/") ? std::string(tr(STR_SD_CARD)) : basepath.substr(basepath.rfind('/') + 1));
  const TextRole folderTitleRole =
      (mode == Mode::PickFirmware || basepath == "/") ? TextRole::System : TextRole::UserContent;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str(), nullptr,
                 folderTitleRole);

  const int pathLineHeight = BaseTheme::getLineHeightForRole(renderer, SMALL_FONT_ID, TextRole::UserContent);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  const int pageItems = std::max(1, contentHeight / std::max(1, metrics.listRowHeight));

  std::string visibleTextKey;
  if (folderTitleRole == TextRole::UserContent) {
    appendTextKey(visibleTextKey, folderName);
  }
  appendTextKey(visibleTextKey, basepath);
  if (!files.empty()) {
    const size_t pageStartIndex = (selectorIndex / static_cast<size_t>(pageItems)) * static_cast<size_t>(pageItems);
    const size_t pageEndIndex = std::min(files.size(), pageStartIndex + static_cast<size_t>(pageItems));
    for (size_t i = pageStartIndex; i < pageEndIndex; ++i) {
      appendTextKey(visibleTextKey, getFileName(files[i]));
      appendTextKey(visibleTextKey, getFileExtension(files[i]));
    }
  }

  if (auto* fcm = renderer.getFontCacheManager();
      fcm != nullptr && visibleTextKey != lastVisibleTextPrewarmKey) {
    fcm->resetRecordedText();
    if (folderTitleRole == TextRole::UserContent) {
      recordRoleText(fcm, UI_12_FONT_ID, folderTitleRole, folderName.c_str(), EpdFontFamily::BOLD);
      recordRoleText(fcm, UI_12_FONT_ID, folderTitleRole, UTF8_ELLIPSIS, EpdFontFamily::BOLD);
    }
    recordRoleText(fcm, SMALL_FONT_ID, TextRole::UserContent, basepath.c_str());
    recordRoleText(fcm, SMALL_FONT_ID, TextRole::UserContent, UTF8_ELLIPSIS);
    if (!files.empty()) {
      const size_t pageStartIndex = (selectorIndex / static_cast<size_t>(pageItems)) * static_cast<size_t>(pageItems);
      const size_t pageEndIndex = std::min(files.size(), pageStartIndex + static_cast<size_t>(pageItems));
      for (size_t i = pageStartIndex; i < pageEndIndex; ++i) {
        const std::string itemName = getFileName(files[i]);
        const std::string itemExtension = getFileExtension(files[i]);
        recordRoleText(fcm, UI_10_FONT_ID, TextRole::UserContent, itemName.c_str());
        recordRoleText(fcm, UI_10_FONT_ID, TextRole::UserContent, itemExtension.c_str());
      }
    }
    fcm->prewarmRecordedText();
    lastVisibleTextPrewarmKey = visibleTextKey;
  }

  if (files.empty()) {
    const char* emptyMsg = (mode == Mode::PickFirmware) ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, emptyMsg);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, files.size(), selectorIndex,
        [this](int index) { return getFileName(files[index]); }, nullptr,
        [this](int index) { return UITheme::getFileIcon(files[index]); },
        [this](int index) { return getFileExtension(files[index]); }, false, TextRole::UserContent);
  }

  // Full path display
  {
    const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
    const int separatorY = pathY - metrics.verticalSpacing / 2;
    renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
    const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
    // Left-truncate so the deepest directory is always visible
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (BaseTheme::getTextWidthForRole(renderer, SMALL_FONT_ID, TextRole::UserContent, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth =
          BaseTheme::getTextWidthForRole(renderer, SMALL_FONT_ID, TextRole::UserContent, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (BaseTheme::getTextWidthForRole(renderer, SMALL_FONT_ID, TextRole::UserContent, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    BaseTheme::drawTextForRole(renderer, SMALL_FONT_ID, TextRole::UserContent, metrics.contentSidePadding, pathY,
                               pathDisplay);
  }

  // Help text
  const char* backLabel = (basepath == "/") ? (mode == Mode::PickFirmware ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const bool selectingFirmwareFile = mode == Mode::PickFirmware && !files.empty() && files[selectorIndex].back() != '/';
  const char* confirmLabel = files.empty() ? "" : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}

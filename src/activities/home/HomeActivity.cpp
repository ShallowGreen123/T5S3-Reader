#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char UTF8_ELLIPSIS[] = "\xE2\x80\xA6";

void appendTextKey(std::string& key, const std::string& text) {
  if (text.empty()) {
    return;
  }
  key.push_back('\n');
  key += text;
}

void recordUserContentText(FontCacheManager* fcm, const int systemFontId, const char* text,
                           const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (fcm == nullptr || text == nullptr || text[0] == '\0') {
    return;
  }
  fcm->recordText(text, BaseTheme::resolveTextFontId(systemFontId, TextRole::UserContent), style);
}
}  // namespace

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Recents, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

bool HomeActivity::needsRecentCovers(int coverHeight) const {
  for (const RecentBook& book : recentBooks) {
    if (book.coverBmpPath.empty()) {
      continue;
    }

    const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
    if (Storage.exists(coverPath.c_str())) {
      continue;
    }

    if (FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path)) {
      return true;
    }
  }

  return false;
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  selectorIndex = 0;
  recentsLoading = false;
  firstRenderDone = false;
  coverRendered = false;
  coverBufferStored = false;
  lastVisibleTextPrewarmKey.clear();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);
  recentsLoaded = !needsRecentCovers(metrics.homeCoverHeight);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
  lastVisibleTextPrewarmKey.clear();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = renderer.getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  // Keep the home clock event-driven. Idle RTC polling here caused periodic
  // redraws and repeated I2C traffic while the device sat on the home screen.
  // Keep SD/EPUB thumbnail work out of the render task; it can be slow enough to stress the render mutex/stack.
  if (firstRenderDone && !recentsLoaded && !recentsLoading) {
    loadRecentCovers(UITheme::getInstance().getMetrics().homeCoverHeight);
    requestUpdate();
    return;
  }

  const int menuCount = getMenuItemCount();

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection(selectorIndex);
  }
}

bool HomeActivity::onTouchTap(int16_t, int16_t y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (!metrics.homeContinueReadingInMenu && !recentBooks.empty() && y >= metrics.homeTopPadding &&
      y < metrics.homeTopPadding + metrics.homeCoverTileHeight) {
    selectorIndex = 0;
    onSelectBook(recentBooks[0].path);
    return true;
  }

  const Rect menuRect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
                      pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                                    metrics.homeMenuTopOffset + metrics.buttonHintsHeight)};
  const int menuCount = getMenuItemCount() - (metrics.homeContinueReadingInMenu ? 0 : recentBooks.size());
  if (menuCount <= 0) return false;

  for (int i = 0; i < menuCount; ++i) {
    const int rowY =
        metrics.verticalSpacing + menuRect.y + static_cast<int>(i) * (metrics.menuRowHeight + metrics.menuSpacing);
    if (y >= rowY && y < rowY + metrics.menuRowHeight) {
      selectorIndex = metrics.homeContinueReadingInMenu ? i : static_cast<int>(recentBooks.size()) + i;
      activateSelection(selectorIndex);
      return true;
    }
  }

  return false;
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  std::string visibleTextKey;
  if (!recentBooks.empty()) {
    appendTextKey(visibleTextKey, recentBooks[0].title);
    appendTextKey(visibleTextKey, recentBooks[0].author);
  }

  if (auto* fcm = renderer.getFontCacheManager();
      fcm != nullptr && visibleTextKey != lastVisibleTextPrewarmKey) {
    fcm->resetRecordedText();
    if (!recentBooks.empty()) {
      recordUserContentText(fcm, UI_12_FONT_ID, recentBooks[0].title.c_str());
      recordUserContentText(fcm, UI_10_FONT_ID, recentBooks[0].author.c_str());
      recordUserContentText(fcm, UI_12_FONT_ID, UTF8_ELLIPSIS);
      recordUserContentText(fcm, UI_10_FONT_ID, UTF8_ELLIPSIS);
      if (metrics.homeContinueReadingInMenu) {
        recordUserContentText(fcm, UI_12_FONT_ID, recentBooks[0].title.c_str(), EpdFontFamily::BOLD);
        recordUserContentText(fcm, UI_12_FONT_ID, UTF8_ELLIPSIS, EpdFontFamily::BOLD);
      }
    }
    fcm->prewarmRecordedText();
    lastVisibleTextPrewarmKey = visibleTextKey;
  }

  char homeClockLabel[6] = {};
  const char* headerClockLabel = nullptr;
  if (!SETTINGS.hideClock && halClock.isAvailable() &&
      halClock.formatTime(homeClockLabel, sizeof(homeClockLabel))) {
    headerClockLabel = homeClockLabel;
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr,
                 nullptr, TextRole::UserContent, TextRole::System, headerClockLabel);

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Settings};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Library);
  }

  if (metrics.homeContinueReadingInMenu) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::activateSelection(int index) {
  int idx = 0;
  int menuSelectedIndex = index - static_cast<int>(recentBooks.size());
  const int fileBrowserIdx = idx++;
  const int recentsIdx = idx++;
  const int opdsLibraryIdx = hasOpdsServers ? idx++ : -1;
  const int fileTransferIdx = idx++;
  const int settingsIdx = idx;

  if (index < static_cast<int>(recentBooks.size())) {
    onSelectBook(recentBooks[index].path);
  } else if (menuSelectedIndex == fileBrowserIdx) {
    onFileBrowserOpen();
  } else if (menuSelectedIndex == recentsIdx) {
    onRecentsOpen();
  } else if (menuSelectedIndex == opdsLibraryIdx) {
    onOpdsBrowserOpen();
  } else if (menuSelectedIndex == fileTransferIdx) {
    onFileTransferOpen();
  } else if (menuSelectedIndex == settingsIdx) {
    onSettingsOpen();
  }
}

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

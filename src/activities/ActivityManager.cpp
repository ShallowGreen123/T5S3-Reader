#include "ActivityManager.h"

#include <HalPowerManager.h>

#include "CrossPointSettings.h"
#include "GlobalMenuActivity.h"
#include "OpdsServerStore.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/HomeActivity.h"
#include "home/RecentBooksActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"
#include "util/FullScreenMessageActivity.h"

namespace {
constexpr HalDisplay::RefreshMode kUiPageTransitionRefreshMode = HalDisplay::HALF_REFRESH;
}  // namespace

void ActivityManager::begin() {
  xTaskCreate(&renderTaskTrampoline, "ActivityManagerRender",
              12288,             // Stack size
              this,              // Parameters
              1,                 // Priority
              &renderTaskHandle  // Task handle
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Acquire the lock before reading currentActivity to avoid a TOCTOU race
    // where the main task deletes the activity between the null-check and render().
    RenderLock lock;
    if (currentActivity) {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      currentActivity->render(std::move(lock));
    }
    // Notify any task blocked in requestUpdateAndWait() that the render is done.
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&waitingTaskMux);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    taskEXIT_CRITICAL(&waitingTaskMux);
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }
  }
}

void ActivityManager::loop() {
  bool injectedTouchButtonTap = false;
  if (currentActivity) {
    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    bool activityHandled = false;
    const bool globalMenuAllowed = currentActivity->supportsGlobalMenu();

    // Global gesture: a drag down from the very top edge opens the global menu overlay.
    if (globalMenuAllowed) {
      MappedInputManager::TouchPoint swipeStart{}, swipeEnd{};
      if (mappedInput.getTouchSwipe(swipeStart, swipeEnd, renderer)) {
        constexpr int kTopBand = 60;  // gesture must start near the top edge
        constexpr int kDragMin = 70;  // and travel down at least this far
        if (swipeStart.y < kTopBand && (swipeEnd.y - swipeStart.y) >= kDragMin) {
          openGlobalMenu();
          activityHandled = true;
        }
      }
    }

    // Home button. With SETTINGS.doubleClickHomeMenu enabled, a double-click opens the
    // global menu; a single click still goes home (deferred by the disambiguation window).
    // Otherwise a single click goes home immediately (original behavior).
    const auto doSingleHome = [this] {
      if (currentActivity && currentActivity->supportsTouchHomeButton() && currentActivity->name != "Home") {
        currentActivity->onGoHome();
      }
    };
    if (!activityHandled && mappedInput.wasTouchHomeButtonPressed()) {
      if (currentActivity->onTouchHomeButton()) {
        // Activity consumed the press (e.g. the global menu dismisses itself).
      } else if (globalMenuAllowed && SETTINGS.doubleClickHomeMenu) {
        if (pendingHomeSingle && millis() - lastHomeEventMs <= kDoubleClickWindowMs) {
          pendingHomeSingle = false;
          openGlobalMenu();
        } else {
          pendingHomeSingle = true;
          lastHomeEventMs = millis();
        }
      } else if (currentActivity->supportsTouchHomeButton() && currentActivity->name != "Home") {
        currentActivity->onGoHome();
      }
      activityHandled = true;
    }
    // Fire the deferred single-home action once the double-click window has elapsed.
    if (pendingHomeSingle && millis() - lastHomeEventMs > kDoubleClickWindowMs) {
      pendingHomeSingle = false;
      doSingleHome();
      activityHandled = true;
    }

    MappedInputManager::TouchPoint touchPoint{};
    bool touchHandled = false;
    if (!activityHandled && mappedInput.wasTouchTapped(touchPoint, renderer)) {
      if (currentActivity->showsHomeTouchButton() && currentActivity->isHomeTouchTap(touchPoint.x, touchPoint.y)) {
        currentActivity->onGoHome();
        touchHandled = true;
      } else {
        MappedInputManager::Button touchButton;
        if (currentActivity->resolveTouchButtonHint(touchPoint.x, touchPoint.y, touchButton)) {
          mappedInput.injectButtonTap(touchButton);
          injectedTouchButtonTap = true;
        } else {
          touchHandled = currentActivity->onTouchTap(touchPoint.x, touchPoint.y);
        }
      }
    }
    if (!activityHandled && !touchHandled) {
      currentActivity->loop();
    }
  }

  if (injectedTouchButtonTap) {
    mappedInput.clearInjectedButtonTap();
  }

  while (pendingAction != PendingAction::None) {
    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;

      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      ActivityResult pendingResult = std::move(currentActivity->result);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        lock.unlock();  // goHome may acquire its own lock
        goHome();
        continue;  // Will launch goHome immediately

      } else {
        currentActivity = std::move(stackActivities.back());
        stackActivities.pop_back();
        LOG_DBG("ACT", "Popped from activity stack, new size = %zu", stackActivities.size());
        renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
        // Handle result if necessary
        if (currentActivity->resultHandler) {
          LOG_DBG("ACT", "Handling result for popped activity");

          // Move it here to avoid the case where handler calling another startActivityForResult()
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();  // Handler may acquire its own lock
          handler(pendingResult);
        }

        // Request an update to ensure the popped activity gets re-rendered
        if (pendingAction == PendingAction::None) {
          requestUpdate();
        }

        // Handler may request another pending action, we will handle it in the next loop iteration
        continue;
      }

    } else if (pendingActivity) {
      // Current activity has requested a new activity to be launched
      RenderLock lock;

      if (pendingAction == PendingAction::Replace) {
        // Destroy the current activity
        exitActivity(lock);
        // Clear the stack
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::Push) {
        // Move current activity to stack
        stackActivities.push_back(std::move(currentActivity));
        LOG_DBG("ACT", "Pushed to activity stack, new size = %zu", stackActivities.size());
      }
      const auto transitionAction = pendingAction;
      const auto replaceRefreshMode = pendingReplaceRefreshMode;
      pendingAction = PendingAction::None;
      pendingReplaceRefreshMode = HalDisplay::FULL_REFRESH;
      currentActivity = std::move(pendingActivity);
      renderer.requestNextRefresh(transitionAction == PendingAction::Replace ? replaceRefreshMode
                                                                             : HalDisplay::HALF_REFRESH);

      lock.unlock();  // onEnter may acquire its own lock
      currentActivity->onEnter();

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  if (requestedUpdate) {
    requestedUpdate = false;
    // Using direct notification to signal the render task to update
    // Increment counter so multiple rapid calls won't be lost
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  }
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  // Note: lock must be held by the caller
  if (currentActivity) {
    currentActivity->onExit();
    currentActivity.reset();
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  replaceActivity(std::move(newActivity), HalDisplay::FULL_REFRESH);
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity,
                                      const HalDisplay::RefreshMode replaceRefreshMode) {
  pendingReplaceRefreshMode = replaceRefreshMode;
  // Note: no lock here, this is usually called by loop() and we may run into deadlock
  if (currentActivity) {
    // Defer launch if we're currently in an activity, to avoid deleting the current activity
    // leading to the "delete this" problem
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    // No current activity, safe to launch immediately
    currentActivity = std::move(newActivity);
    currentActivity->onEnter();
  }
}

void ActivityManager::goToFileTransfer() {
  replaceActivity(std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput), kUiPageTransitionRefreshMode);
}

void ActivityManager::goToSettings() {
  replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput), kUiPageTransitionRefreshMode);
}

void ActivityManager::goToFileBrowser(std::string path) {
  replaceActivity(std::make_unique<FileBrowserActivity>(renderer, mappedInput, std::move(path)),
                  kUiPageTransitionRefreshMode);
}

void ActivityManager::goToRecentBooks() {
  replaceActivity(std::make_unique<RecentBooksActivity>(renderer, mappedInput), kUiPageTransitionRefreshMode);
}

void ActivityManager::goToBrowser() {
  const auto& servers = OPDS_STORE.getServers();
  // Skip the server picker when there's only one server configured
  if (servers.size() == 1) {
    replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, servers[0]));
  } else {
    replaceActivity(std::make_unique<OpdsServerListActivity>(renderer, mappedInput, true));
  }
}

void ActivityManager::goToReader(std::string path, const HalDisplay::RefreshMode replaceRefreshMode) {
  replaceActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, std::move(path), replaceRefreshMode),
                  replaceRefreshMode);
}

void ActivityManager::goToSleep() {
  replaceActivity(std::make_unique<SleepActivity>(renderer, mappedInput));
  loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
}

void ActivityManager::goToBoot() { replaceActivity(std::make_unique<BootActivity>(renderer, mappedInput)); }

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
}

void ActivityManager::goToCrashReport() { replaceActivity(std::make_unique<CrashActivity>(renderer, mappedInput)); }

void ActivityManager::goHome() {
  replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInput), kUiPageTransitionRefreshMode);
}

void ActivityManager::openGlobalMenu() {
  // Guard against opening over an activity that doesn't allow it (or over itself).
  if (!currentActivity || !currentActivity->supportsGlobalMenu() || pendingActivity) {
    return;
  }
  // Readers leave the panel in a grayscale state; the menu must full-refresh to render
  // crisply over it (see GlobalMenuActivity).
  const bool overReader = currentActivity->isReaderActivity();
  pushActivity(std::make_unique<GlobalMenuActivity>(renderer, mappedInput, overReader));
}

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while pushActivity is not expected");
    pendingActivity.reset();
  }
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
}

void ActivityManager::popActivity() {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::isReaderActivity() const { return currentActivity && currentActivity->isReaderActivity(); }

bool ActivityManager::isReaderActivityInStack() const {
  if (currentActivity && currentActivity->isReaderActivity()) return true;
  for (const auto& act : stackActivities) {
    if (act && act->isReaderActivity()) return true;
  }
  return false;
}

bool ActivityManager::isReaderPageActivity() const {
  return currentActivity && (currentActivity->name == "EpubReader" || currentActivity->name == "TxtReader" ||
                             currentActivity->name == "XtcReader");
}

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  if (currentActivity) {
    return currentActivity->getScreenshotInfo();
  }
  return {};
}

void ActivityManager::requestUpdate(bool immediate) {
  if (immediate) {
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  } else {
    // Deferring the update until current loop is finished
    // This is to avoid multiple updates being requested in the same loop
    requestedUpdate = true;
  }
}
void ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return;
  }

  // Atomic section to perform checks
  taskENTER_CRITICAL(&waitingTaskMux);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&waitingTaskMux);

  // Render task cannot call requestUpdateAndWait() or it will cause a deadlock
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");

  // There should never be the case where 2 tasks are waiting for a render at the same time
  assert(!alreadyWaiting && "Already waiting for a render to complete");

  // Cannot call while holding RenderLock or it will cause a deadlock
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");

  xTaskNotify(renderTaskHandle, 1, eIncrement);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

// RenderLock

RenderLock::RenderLock() {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
  ownerTask = xTaskGetCurrentTaskHandle();
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
  ownerTask = xTaskGetCurrentTaskHandle();
}

RenderLock::~RenderLock() {
  unlock();
}

void RenderLock::unlock() {
  if (!isLocked) {
    return;
  }

  TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
  TaskHandle_t holderTask = xSemaphoreGetMutexHolder(activityManager.renderingMutex);
  if (ownerTask == nullptr) {
    ownerTask = holderTask;
  }

  if (ownerTask == nullptr || holderTask != ownerTask || currentTask != ownerTask) {
    LOG_ERR("ACT", "RenderLock unlock skipped: owner=%p holder=%p current=%p", ownerTask, holderTask, currentTask);
    isLocked = false;
    ownerTask = nullptr;
    return;
  }

  xSemaphoreGive(activityManager.renderingMutex);
  isLocked = false;
  ownerTask = nullptr;
}

/**
 *
 * Checks if renderingMutex is busy.
 *
 * @return true if renderingMutex is busy, otherwise false.
 *
 */
bool RenderLock::peek() { return xQueuePeek(activityManager.renderingMutex, NULL, 0) != pdTRUE; };

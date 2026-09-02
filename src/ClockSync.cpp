#include "ClockSync.h"

#include <HalClock.h>
#include <Logging.h>
#include <esp_sntp.h>
#include <time.h>

#include "CrossPointSettings.h"

namespace ClockSync {
namespace {
constexpr time_t kResyncIntervalSeconds = 12 * 60 * 60;
constexpr const char* kNtpServers[] = {
    "pool.ntp.org",
    "time.cloudflare.com",
    "ntp.aliyun.com",
};

bool hasNetworkSync = false;
time_t lastNetworkSyncEpoch = 0;

bool shouldSync(const bool force) {
  if (force || !halClock.isSystemTimeValid()) {
    return true;
  }

  if (!hasNetworkSync) {
    return true;
  }

  time_t now = 0;
  time(&now);
  return now <= lastNetworkSyncEpoch || (now - lastNetworkSyncEpoch) >= kResyncIntervalSeconds;
}

bool waitForSync(const uint32_t timeoutMs) {
  const unsigned long startMs = millis();
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && (millis() - startMs) < timeoutMs) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  return halClock.isSystemTimeValid() && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
}
}  // namespace

void stop() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
}

bool commitCurrentSystemTime(const char* sourceTag) {
  time_t now = 0;
  time(&now);
  if (!halClock.isSystemTimeValid()) {
    LOG_ERR("CLK", "%s time commit skipped: system time invalid", sourceTag != nullptr ? sourceTag : "System");
    return false;
  }

  hasNetworkSync = true;
  lastNetworkSyncEpoch = now;

  if (!halClock.syncRtcFromSystemTime()) {
    LOG_ERR("CLK", "%s time commit failed: RTC write-back failed", sourceTag != nullptr ? sourceTag : "System");
    return false;
  }

  bool needsSave = false;
  if (SETTINGS.rtcStoresUtc == 0) {
    SETTINGS.rtcStoresUtc = 1;
    needsSave = true;
  }
  const uint8_t variantHint = halClock.getVariantHint();
  if (SETTINGS.rtcVariantHint != variantHint) {
    SETTINGS.rtcVariantHint = variantHint;
    needsSave = true;
  }
  const uint32_t referenceEpoch = static_cast<uint32_t>(now);
  if (SETTINGS.rtcReferenceEpoch != referenceEpoch) {
    SETTINGS.rtcReferenceEpoch = referenceEpoch;
    needsSave = true;
  }
  if (needsSave) {
    if (!SETTINGS.saveToFile()) {
      LOG_ERR("CLK", "Failed to persist RTC clock settings after %s sync", sourceTag != nullptr ? sourceTag : "system");
    }
  }

  halClock.configure(SETTINGS.timeZoneId, true, SETTINGS.rtcVariantHint, SETTINGS.rtcReferenceEpoch);
  LOG_DBG("CLK", "%s time synced and RTC updated", sourceTag != nullptr ? sourceTag : "System");
  return true;
}

bool syncWithNtp(const uint32_t timeoutMs, const bool force) {
  if (!shouldSync(force)) {
    return true;
  }

  const unsigned long syncStartMs = millis();
  const unsigned long deadlineMs = syncStartMs + timeoutMs;
  const char* syncedServer = nullptr;
  bool synced = false;

  for (size_t i = 0; i < (sizeof(kNtpServers) / sizeof(kNtpServers[0])); ++i) {
    const unsigned long nowMs = millis();
    if (nowMs >= deadlineMs) {
      break;
    }

    const size_t serversLeft = (sizeof(kNtpServers) / sizeof(kNtpServers[0])) - i;
    const uint32_t remainingMs = static_cast<uint32_t>(deadlineMs - nowMs);
    uint32_t attemptTimeoutMs = remainingMs / static_cast<uint32_t>(serversLeft);
    if (attemptTimeoutMs == 0) {
      attemptTimeoutMs = remainingMs;
    } else if (attemptTimeoutMs < 1500 && remainingMs > 1500) {
      attemptTimeoutMs = 1500;
    }
    if (attemptTimeoutMs > remainingMs) {
      attemptTimeoutMs = remainingMs;
    }

    stop();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, const_cast<char*>(kNtpServers[i]));
    esp_sntp_init();

    if (waitForSync(attemptTimeoutMs)) {
      synced = true;
      syncedServer = kNtpServers[i];
      break;
    }

    stop();
    LOG_DBG("CLK", "NTP sync attempt failed for %s after %lu ms", kNtpServers[i],
            static_cast<unsigned long>(attemptTimeoutMs));
  }

  stop();

  if (!synced) {
    LOG_DBG("CLK", "NTP sync skipped/failed after %lu ms", static_cast<unsigned long>(timeoutMs));
    return false;
  }

  LOG_DBG("CLK", "NTP sync succeeded via %s", syncedServer != nullptr ? syncedServer : "unknown");
  return commitCurrentSystemTime("NTP");
}

}  // namespace ClockSync

#include "TimeZoneCatalog.h"

#include <cstring>

namespace TimeZoneCatalog {
namespace {

constexpr const char* kUtcId = "UTC";
constexpr const char* kUtcPosix = "UTC0";

constexpr const char* kRegionIds[] = {
    "UTC", "Africa", "America", "Antarctica", "Arctic", "Asia", "Atlantic", "Australia", "Europe", "Indian", "Pacific",
};

constexpr const char* kRegionDisplayNames[] = {
    "UTC",
    "Africa",
    "America",
    "Antarctica",
    "Arctic",
    "Asia",
    "Atlantic",
    "Australia",
    "Europe",
    "Indian Ocean",
    "Pacific",
};

constexpr const char* kLegacyIds[kLegacyCount] = {
    "UTC",
    "Asia/Shanghai",
    "Europe/London",
    "Europe/Berlin",
    "Europe/Helsinki",
    "America/New_York",
    "America/Chicago",
    "America/Denver",
    "America/Phoenix",
    "America/Los_Angeles",
    "America/Anchorage",
    "Pacific/Honolulu",
};

bool idsEqual(const char* a, const char* b) { return a != nullptr && b != nullptr && strcmp(a, b) == 0; }

}  // namespace

const TimeZoneEntry& get(const uint16_t index) {
  const uint16_t n = count();
  if (index >= n) {
    return data()[0];
  }
  return data()[index];
}

int16_t indexOf(const char* id) {
  if (id == nullptr || id[0] == '\0') {
    return -1;
  }
  if (idsEqual(id, "Etc/UTC") || idsEqual(id, "Etc/GMT")) {
    id = kUtcId;
  }

  const auto* entries = data();
  const uint16_t n = count();
  for (uint16_t i = 0; i < n; ++i) {
    if (idsEqual(entries[i].id, id)) {
      return static_cast<int16_t>(i);
    }
  }
  return -1;
}

bool isValidId(const char* id) { return indexOf(id) >= 0; }

const char* posixRule(const char* id) {
  const int16_t index = indexOf(id);
  if (index < 0) {
    return kUtcPosix;
  }
  return get(static_cast<uint16_t>(index)).posix;
}

TimeZoneRegion regionOf(const char* id) {
  const int16_t index = indexOf(id);
  if (index < 0) {
    return TimeZoneRegion::UTC;
  }
  return get(static_cast<uint16_t>(index)).region;
}

const char* regionId(const TimeZoneRegion region) {
  const auto index = static_cast<uint8_t>(region);
  if (index >= static_cast<uint8_t>(TimeZoneRegion::Count)) {
    return kRegionIds[0];
  }
  return kRegionIds[index];
}

const char* regionDisplayName(const TimeZoneRegion region) {
  const auto index = static_cast<uint8_t>(region);
  if (index >= static_cast<uint8_t>(TimeZoneRegion::Count)) {
    return kRegionDisplayNames[0];
  }
  return kRegionDisplayNames[index];
}

uint16_t countInRegion(const TimeZoneRegion region) {
  const auto* entries = data();
  const uint16_t n = count();
  uint16_t total = 0;
  for (uint16_t i = 0; i < n; ++i) {
    if (entries[i].region == region) {
      ++total;
    }
  }
  return total;
}

uint16_t indexInRegion(const TimeZoneRegion region, const uint16_t cityIndex) {
  const auto* entries = data();
  const uint16_t n = count();
  uint16_t seen = 0;
  for (uint16_t i = 0; i < n; ++i) {
    if (entries[i].region != region) {
      continue;
    }
    if (seen == cityIndex) {
      return i;
    }
    ++seen;
  }
  return 0;
}

void formatDisplayName(const char* id, char* buf, const size_t bufSize) {
  if (buf == nullptr || bufSize == 0) {
    return;
  }
  if (id == nullptr || id[0] == '\0' || idsEqual(id, kUtcId) || idsEqual(id, "Etc/UTC") || idsEqual(id, "Etc/GMT")) {
    strncpy(buf, kUtcId, bufSize - 1);
    buf[bufSize - 1] = '\0';
    return;
  }

  const char* slash = strchr(id, '/');
  const char* src = slash != nullptr ? slash + 1 : id;
  size_t out = 0;
  for (; src[0] != '\0' && out + 1 < bufSize; ++src) {
    buf[out++] = src[0] == '_' ? ' ' : src[0];
  }
  buf[out] = '\0';
}

const char* displayName(const char* id) {
  static char buf[kMaxIdLength];
  formatDisplayName(id, buf, sizeof(buf));
  return buf;
}

const char* idFromLegacyIndex(const uint8_t index) {
  if (index >= kLegacyCount) {
    return kUtcId;
  }
  return kLegacyIds[index];
}

void copyId(char* dest, const size_t destSize, const char* id) {
  if (dest == nullptr || destSize == 0) {
    return;
  }
  const int16_t index = indexOf(id);
  const char* src = index >= 0 ? get(static_cast<uint16_t>(index)).id : kUtcId;
  strncpy(dest, src, destSize - 1);
  dest[destSize - 1] = '\0';
}

}  // namespace TimeZoneCatalog

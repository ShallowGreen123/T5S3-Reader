#pragma once

#include <cstddef>
#include <cstdint>

enum class TimeZoneRegion : uint8_t {
  UTC = 0,
  Africa,
  America,
  Antarctica,
  Arctic,
  Asia,
  Atlantic,
  Australia,
  Europe,
  Indian,
  Pacific,
  Count
};

struct TimeZoneEntry {
  const char* id;
  const char* posix;
  TimeZoneRegion region;
};

namespace TimeZoneCatalog {

constexpr uint16_t kMaxIdLength = 40;
constexpr uint16_t kMaxCitiesPerRegion = 160;
constexpr uint8_t kLegacyCount = 12;

const TimeZoneEntry* data();
uint16_t count();

const TimeZoneEntry& get(uint16_t index);
int16_t indexOf(const char* id);
bool isValidId(const char* id);
const char* posixRule(const char* id);
TimeZoneRegion regionOf(const char* id);
const char* regionId(TimeZoneRegion region);
const char* regionDisplayName(TimeZoneRegion region);

uint16_t countInRegion(TimeZoneRegion region);
uint16_t indexInRegion(TimeZoneRegion region, uint16_t cityIndex);

// Formats "America/Argentina/Buenos_Aires" as "Argentina/Buenos Aires".
void formatDisplayName(const char* id, char* buf, size_t bufSize);
const char* displayName(const char* id);

// Maps firmware v1 enum values (0-11) to IANA ids.
const char* idFromLegacyIndex(uint8_t index);

void copyId(char* dest, size_t destSize, const char* id);

}  // namespace TimeZoneCatalog

#include "HalClock.h"

#include <Board.h>
#include <Logging.h>
#include <Wire.h>
#include <sys/time.h>
#include <time.h>

#include <cstdlib>

namespace {
constexpr uint8_t kRtcAddress = BoardPins::RtcAddress;
constexpr uint8_t kRegisterCount = 7;
constexpr time_t kUsableSystemTimeEpoch = 946684800;   // 2000-01-01 00:00:00 UTC
constexpr time_t kValidSystemTimeEpoch = 1704067200;  // 2024-01-01 00:00:00 UTC
constexpr const char* kTimeZoneRules[HalClock::TIME_ZONE_COUNT] = {
    "UTC0",
    "CST-8",
    "GMT0BST,M3.5.0/1,M10.5.0/2",
    "CET-1CEST,M3.5.0/2,M10.5.0/3",
    "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "EST5EDT,M3.2.0/2,M11.1.0/2",
    "CST6CDT,M3.2.0/2,M11.1.0/2",
    "MST7MDT,M3.2.0/2,M11.1.0/2",
    "MST7",
    "PST8PDT,M3.2.0/2,M11.1.0/2",
    "AKST9AKDT,M3.2.0/2,M11.1.0/2",
    "HST10",
};

uint8_t bcdToDec(const uint8_t bcd) { return static_cast<uint8_t>(((bcd >> 4) * 10) + (bcd & 0x0F)); }

uint8_t decToBcd(const uint8_t dec) { return static_cast<uint8_t>(((dec / 10) << 4) | (dec % 10)); }

bool isValidBcd(const uint8_t bcd, const uint8_t maxValue) {
  if ((bcd & 0x0F) > 9 || ((bcd >> 4) & 0x0F) > 9) {
    return false;
  }
  return bcdToDec(bcd) <= maxValue;
}

bool isLeapYear(const int year) {
  if ((year % 4) != 0) {
    return false;
  }
  if ((year % 100) != 0) {
    return true;
  }
  return (year % 400) == 0;
}

uint8_t daysInMonth(const int year, const uint8_t month) {
  static constexpr uint8_t kDaysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 0;
  }

  if (month == 2 && isLeapYear(year)) {
    return 29;
  }

  return kDaysPerMonth[month - 1];
}

int64_t daysFromCivil(int year, const uint8_t month, const uint8_t day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const int monthIndex = static_cast<int>(month) + (month > 2 ? -3 : 9);
  const unsigned doy =
      (153u * static_cast<unsigned>(monthIndex) + 2u) / 5u + static_cast<unsigned>(day) - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}
}  // namespace

HalClock halClock;

void HalClock::begin() {
  available_ = false;
  variant_ = ChipVariant::Unknown;

  const bool hasPcf85063 = probeVariant(ChipVariant::Pcf85063);
  const bool hasPcf8563 = probeVariant(ChipVariant::Pcf8563);

  if (hasPcf85063) {
    variant_ = ChipVariant::Pcf85063;
    available_ = true;
    LOG_INF("CLK", hasPcf8563 ? "RTC detected at 0x51 (PCF85063/PCF8563-compatible, using PCF85063 layout)"
                              : "RTC detected at 0x51 (PCF85063 layout)");
  } else if (hasPcf8563) {
    variant_ = ChipVariant::Pcf8563;
    available_ = true;
    LOG_INF("CLK", "RTC detected at 0x51 (PCF8563 layout)");
  } else {
    LOG_INF("CLK", "RTC not found at 0x51");
  }
}

void HalClock::configure(const uint8_t timeZone, const bool rtcStoresUtc, const uint8_t rtcVariantHint,
                         const uint32_t rtcReferenceEpoch) {
  timeZone_ = timeZone < TIME_ZONE_COUNT ? timeZone : TIME_ZONE_UTC;
  rtcStoresUtc_ = rtcStoresUtc;
  rtcReferenceEpoch_ = rtcReferenceEpoch;
  const ChipVariant hintedVariant = chipVariantFromHint(rtcVariantHint);
  if (hintedVariant != ChipVariant::Unknown && available_) {
    variant_ = hintedVariant;
  }
  applyTimeZone();
}

uint8_t HalClock::getVariantHint() const { return chipVariantToHint(variant_); }

bool HalClock::isSystemTimeValid() const {
  time_t now = 0;
  time(&now);
  return isSystemTimeValid(now);
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  time_t now = 0;
  time(&now);
  if (!isSystemTimeUsable(now)) {
    if (!const_cast<HalClock*>(this)->syncSystemTimeFromRtc()) {
      return false;
    }
    time(&now);
  }

  if (!isSystemTimeUsable(now)) {
    return false;
  }

  tm localTime = {};
  if (localtime_r(&now, &localTime) == nullptr) {
    return false;
  }

  hour = static_cast<uint8_t>(localTime.tm_hour);
  minute = static_cast<uint8_t>(localTime.tm_min);
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize) const {
  if (buf == nullptr || bufSize < 6) {
    return false;
  }

  uint8_t hour = 0;
  uint8_t minute = 0;
  if (!getTime(hour, minute)) {
    return false;
  }

  snprintf(buf, bufSize, "%02u:%02u", static_cast<unsigned>(hour), static_cast<unsigned>(minute));
  return true;
}

bool HalClock::syncSystemTimeFromRtc() {
  DateTime dateTime;
  if (!readDateTime(dateTime)) {
    LOG_DBG("CLK", "RTC read failed for layout hint %u", static_cast<unsigned>(chipVariantToHint(variant_)));
    return false;
  }

  const time_t utcEpoch = makeUtcEpoch(dateTime);
  const time_t localEpoch = makeLocalEpoch(dateTime);
  const bool utcUsable = isSystemTimeUsable(utcEpoch);
  const bool localUsable = isSystemTimeUsable(localEpoch);
  bool selectedStoresUtc = rtcStoresUtc_;

  if (utcUsable && localUsable && isSystemTimeUsable(static_cast<time_t>(rtcReferenceEpoch_))) {
    const int64_t utcDelta = llabs(static_cast<int64_t>(utcEpoch) - static_cast<int64_t>(rtcReferenceEpoch_));
    const int64_t localDelta = llabs(static_cast<int64_t>(localEpoch) - static_cast<int64_t>(rtcReferenceEpoch_));
    constexpr int64_t kSwitchThresholdSeconds = 30 * 60;
    if (utcDelta + kSwitchThresholdSeconds < localDelta) {
      selectedStoresUtc = true;
    } else if (localDelta + kSwitchThresholdSeconds < utcDelta) {
      selectedStoresUtc = false;
    }
  } else if (utcUsable && !localUsable) {
    selectedStoresUtc = true;
  } else if (localUsable && !utcUsable) {
    selectedStoresUtc = false;
  }

  if (selectedStoresUtc != rtcStoresUtc_) {
    LOG_INF("CLK", "RTC storage mode auto-corrected at boot: %s -> %s", rtcStoresUtc_ ? "UTC" : "Local",
            selectedStoresUtc ? "UTC" : "Local");
    rtcStoresUtc_ = selectedStoresUtc;
  }

  const time_t epoch = rtcStoresUtc_ ? utcEpoch : localEpoch;
  if (!isSystemTimeUsable(epoch)) {
    LOG_DBG("CLK", "RTC contained unusable time %04u-%02u-%02u %02u:%02u:%02u (utc=%lld local=%lld)",
            static_cast<unsigned>(dateTime.year), static_cast<unsigned>(dateTime.month), static_cast<unsigned>(dateTime.day),
            static_cast<unsigned>(dateTime.hour), static_cast<unsigned>(dateTime.minute),
            static_cast<unsigned>(dateTime.second), static_cast<long long>(utcEpoch), static_cast<long long>(localEpoch));
    return false;
  }

  const timeval tv = {
      .tv_sec = epoch,
      .tv_usec = 0,
  };
  const bool ok = settimeofday(&tv, nullptr) == 0;
  LOG_DBG("CLK", "RTC boot read %04u-%02u-%02u %02u:%02u:%02u using %s layout=%u ref=%lu -> epoch=%lld result=%d",
          static_cast<unsigned>(dateTime.year), static_cast<unsigned>(dateTime.month), static_cast<unsigned>(dateTime.day),
          static_cast<unsigned>(dateTime.hour), static_cast<unsigned>(dateTime.minute), static_cast<unsigned>(dateTime.second),
          rtcStoresUtc_ ? "UTC" : "Local", static_cast<unsigned>(chipVariantToHint(variant_)),
          static_cast<unsigned long>(rtcReferenceEpoch_), static_cast<long long>(epoch), ok ? 1 : 0);
  return ok;
}

bool HalClock::syncRtcFromSystemTime() {
  if (!available_) {
    return false;
  }

  time_t now = 0;
  time(&now);
  if (!isSystemTimeValid(now)) {
    return false;
  }

  tm utcTime = {};
  if (gmtime_r(&now, &utcTime) == nullptr) {
    return false;
  }

  const DateTime expectedDateTime = {
      .year = static_cast<uint16_t>(utcTime.tm_year + 1900),
      .month = static_cast<uint8_t>(utcTime.tm_mon + 1),
      .day = static_cast<uint8_t>(utcTime.tm_mday),
      .weekday = static_cast<uint8_t>(utcTime.tm_wday),
      .hour = static_cast<uint8_t>(utcTime.tm_hour),
      .minute = static_cast<uint8_t>(utcTime.tm_min),
      .second = static_cast<uint8_t>(utcTime.tm_sec),
  };

  const auto tryWriteVariant = [this, &expectedDateTime](const ChipVariant variant) -> bool {
    if (variant == ChipVariant::Unknown) {
      return false;
    }

    uint8_t data[kRegisterCount] = {};
    if (!encodeRegisters(variant, expectedDateTime, data, sizeof(data))) {
      return false;
    }
    if (!writeRegisters(timeStartRegister(variant), data, sizeof(data))) {
      return false;
    }

    DateTime actualDateTime;
    if (!readDateTime(variant, actualDateTime)) {
      return false;
    }
    if (!dateTimeMatches(expectedDateTime, actualDateTime)) {
      LOG_DBG("CLK", "RTC write-back verify failed for layout %u", static_cast<unsigned>(chipVariantToHint(variant)));
      return false;
    }

    variant_ = variant;
    return true;
  };

  if (tryWriteVariant(variant_)) {
    return true;
  }

  const ChipVariant alternate = alternateVariant(variant_);
  if (tryWriteVariant(alternate)) {
    LOG_INF("CLK", "RTC layout switched to %s after verification",
            alternate == ChipVariant::Pcf8563 ? "PCF8563" : "PCF85063");
    return true;
  }

  return false;
}

bool HalClock::probeVariant(const ChipVariant variant) {
  DateTime dateTime;
  uint8_t data[kRegisterCount] = {};
  return readRegisters(timeStartRegister(variant), data, sizeof(data)) && decodeRegisters(variant, data, dateTime);
}

bool HalClock::readDateTime(DateTime& dateTime) const {
  if (!available_) {
    return false;
  }

  return readDateTime(variant_, dateTime);
}

bool HalClock::readDateTime(const ChipVariant variant, DateTime& dateTime) const {
  if (!available_ || variant == ChipVariant::Unknown) {
    return false;
  }

  uint8_t data[kRegisterCount] = {};
  return readRegisters(timeStartRegister(variant), data, sizeof(data)) && decodeRegisters(variant, data, dateTime);
}

bool HalClock::readRegisters(const uint8_t startReg, uint8_t* data, const size_t len) const {
  if (data == nullptr || len == 0) {
    return false;
  }

  Board::ScopedI2CLock lock;
  Wire.beginTransmission(kRtcAddress);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const uint8_t requested = static_cast<uint8_t>(len);
  if (Wire.requestFrom(kRtcAddress, requested) != requested) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

bool HalClock::writeRegisters(const uint8_t startReg, const uint8_t* data, const size_t len) const {
  if (data == nullptr || len == 0) {
    return false;
  }

  Board::ScopedI2CLock lock;
  Wire.beginTransmission(kRtcAddress);
  Wire.write(startReg);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

uint8_t HalClock::timeStartRegister(const ChipVariant variant) {
  switch (variant) {
    case ChipVariant::Pcf85063:
      return 0x04;
    case ChipVariant::Pcf8563:
    case ChipVariant::Unknown:
    default:
      return 0x02;
  }
}

bool HalClock::decodeRegisters(const ChipVariant variant, const uint8_t* data, DateTime& dateTime) {
  if (data == nullptr) {
    return false;
  }

  const uint8_t rawSeconds = data[0];
  const uint8_t rawMinutes = data[1] & 0x7F;
  const uint8_t rawHours = data[2] & 0x3F;
  const uint8_t rawDays = data[3] & 0x3F;
  const uint8_t rawWeekdays = data[4] & 0x07;
  const uint8_t rawMonths = data[5] & 0x1F;
  const uint8_t rawYears = data[6];

  if ((variant == ChipVariant::Pcf85063 || variant == ChipVariant::Pcf8563) && (rawSeconds & 0x80) != 0) {
    return false;
  }

  if (!isValidBcd(rawSeconds & 0x7F, 59) || !isValidBcd(rawMinutes, 59) || !isValidBcd(rawHours, 23) ||
      !isValidBcd(rawDays, 31) || !isValidBcd(rawMonths, 12) || !isValidBcd(rawYears, 99)) {
    return false;
  }

  dateTime.year = static_cast<uint16_t>(2000 + bcdToDec(rawYears));
  dateTime.month = bcdToDec(rawMonths);
  dateTime.day = bcdToDec(rawDays);
  dateTime.weekday = rawWeekdays;
  dateTime.hour = bcdToDec(rawHours);
  dateTime.minute = bcdToDec(rawMinutes);
  dateTime.second = bcdToDec(rawSeconds & 0x7F);

  if (dateTime.month == 0 || dateTime.month > 12 || dateTime.day == 0 ||
      dateTime.day > daysInMonth(dateTime.year, dateTime.month) || dateTime.weekday > 6) {
    return false;
  }

  return true;
}

bool HalClock::encodeRegisters(const ChipVariant variant, const DateTime& dateTime, uint8_t* data, const size_t len) {
  if (data == nullptr || len < kRegisterCount) {
    return false;
  }

  if (dateTime.year < 2000 || dateTime.year > 2099 || dateTime.month < 1 || dateTime.month > 12 || dateTime.day < 1 ||
      dateTime.day > daysInMonth(dateTime.year, dateTime.month) || dateTime.weekday > 6 || dateTime.hour > 23 ||
      dateTime.minute > 59 || dateTime.second > 59) {
    return false;
  }

  data[0] = decToBcd(dateTime.second);
  data[1] = decToBcd(dateTime.minute);
  data[2] = decToBcd(dateTime.hour);
  data[3] = decToBcd(dateTime.day);
  data[4] = static_cast<uint8_t>(dateTime.weekday & 0x07);
  data[5] = decToBcd(dateTime.month);
  data[6] = decToBcd(static_cast<uint8_t>(dateTime.year - 2000));

  if (variant == ChipVariant::Pcf8563) {
    data[5] &= 0x1F;  // Century bit cleared for 2000-2099.
  }

  return true;
}

HalClock::ChipVariant HalClock::chipVariantFromHint(const uint8_t hint) {
  switch (hint) {
    case 1:
      return ChipVariant::Pcf85063;
    case 2:
      return ChipVariant::Pcf8563;
    default:
      return ChipVariant::Unknown;
  }
}

uint8_t HalClock::chipVariantToHint(const ChipVariant variant) {
  switch (variant) {
    case ChipVariant::Pcf85063:
      return 1;
    case ChipVariant::Pcf8563:
      return 2;
    case ChipVariant::Unknown:
    default:
      return 0;
  }
}

HalClock::ChipVariant HalClock::alternateVariant(const ChipVariant variant) {
  switch (variant) {
    case ChipVariant::Pcf85063:
      return ChipVariant::Pcf8563;
    case ChipVariant::Pcf8563:
      return ChipVariant::Pcf85063;
    case ChipVariant::Unknown:
    default:
      return ChipVariant::Unknown;
  }
}

bool HalClock::dateTimeMatches(const DateTime& expected, const DateTime& actual) {
  const int64_t delta =
      static_cast<int64_t>(makeUtcEpoch(actual)) - static_cast<int64_t>(makeUtcEpoch(expected));
  return delta >= -1 && delta <= 1;
}

bool HalClock::isSystemTimeUsable(const time_t epoch) { return epoch >= kUsableSystemTimeEpoch; }

bool HalClock::isSystemTimeValid(const time_t epoch) { return epoch >= kValidSystemTimeEpoch; }

time_t HalClock::makeUtcEpoch(const DateTime& dateTime) {
  const int64_t days = daysFromCivil(static_cast<int>(dateTime.year), dateTime.month, dateTime.day);
  return static_cast<time_t>(days * 86400LL + static_cast<int64_t>(dateTime.hour) * 3600LL +
                             static_cast<int64_t>(dateTime.minute) * 60LL + dateTime.second);
}

time_t HalClock::makeLocalEpoch(const DateTime& dateTime) {
  tm localTime = {};
  localTime.tm_year = static_cast<int>(dateTime.year) - 1900;
  localTime.tm_mon = static_cast<int>(dateTime.month) - 1;
  localTime.tm_mday = dateTime.day;
  localTime.tm_hour = dateTime.hour;
  localTime.tm_min = dateTime.minute;
  localTime.tm_sec = dateTime.second;
  localTime.tm_wday = dateTime.weekday;
  localTime.tm_isdst = -1;
  return mktime(&localTime);
}

void HalClock::applyTimeZone() const {
  const uint8_t index = timeZone_ < TIME_ZONE_COUNT ? timeZone_ : TIME_ZONE_UTC;
  setenv("TZ", kTimeZoneRules[index], 1);
  tzset();
}

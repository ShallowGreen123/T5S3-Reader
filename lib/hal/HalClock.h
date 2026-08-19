#pragma once

#include <Arduino.h>

class HalClock;
extern HalClock halClock;

class HalClock {
 public:
  void begin();
  void configure(const char* timeZoneId, bool rtcStoresUtc, uint8_t rtcVariantHint = 0,
                 uint32_t rtcReferenceEpoch = 0);
  bool isAvailable() const { return available_; }
  uint8_t getVariantHint() const;
  bool getRtcStoresUtc() const { return rtcStoresUtc_; }
  bool isSystemTimeValid() const;
  bool getTime(uint8_t& hour, uint8_t& minute) const;
  bool formatTime(char* buf, size_t bufSize) const;
  bool syncSystemTimeFromRtc();
  bool syncRtcFromSystemTime();

 private:
  struct DateTime {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t weekday = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
  };

  enum class ChipVariant : uint8_t { Unknown = 0, Pcf85063, Pcf8563 };

  bool probeVariant(ChipVariant variant);
  bool readDateTime(DateTime& dateTime) const;
  bool readDateTime(ChipVariant variant, DateTime& dateTime) const;
  bool readRegisters(uint8_t startReg, uint8_t* data, size_t len) const;
  bool writeRegisters(uint8_t startReg, const uint8_t* data, size_t len) const;
  static uint8_t timeStartRegister(ChipVariant variant);
  static bool decodeRegisters(ChipVariant variant, const uint8_t* data, DateTime& dateTime);
  static bool encodeRegisters(ChipVariant variant, const DateTime& dateTime, uint8_t* data, size_t len);
  static ChipVariant chipVariantFromHint(uint8_t hint);
  static uint8_t chipVariantToHint(ChipVariant variant);
  static ChipVariant alternateVariant(ChipVariant variant);
  static bool dateTimeMatches(const DateTime& expected, const DateTime& actual);
  static bool isSystemTimeUsable(time_t epoch);
  static bool isSystemTimeValid(time_t epoch);
  static time_t makeUtcEpoch(const DateTime& dateTime);
  static time_t makeLocalEpoch(const DateTime& dateTime);
  void applyTimeZone() const;

  ChipVariant variant_ = ChipVariant::Unknown;
  bool available_ = false;
  const char* posixRule_ = "UTC0";
  bool rtcStoresUtc_ = false;
  uint32_t rtcReferenceEpoch_ = 0;
};

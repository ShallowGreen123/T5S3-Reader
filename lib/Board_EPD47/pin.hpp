#pragma once

#include <Arduino.h>

#define EPD47_WIDTH 960
#define EPD47_HEIGHT 540
#define EPD47_LOGICAL_WIDTH 540
#define EPD47_LOGICAL_HEIGHT 960

#define EPD47_I2C_SDA 18
#define EPD47_I2C_SCL 17
#define EPD47_I2C_FREQ 400000

#define EPD47_TOUCH_INT 47
#define EPD47_BUTTON 21
#define EPD47_BATTERY_ADC 14

#define EPD47_SD_MISO 16
#define EPD47_SD_MOSI 15
#define EPD47_SD_SCLK 11
#define EPD47_SD_CS 42

namespace BoardEPD47Pins {
static constexpr uint16_t DisplayWidth = EPD47_WIDTH;
static constexpr uint16_t DisplayHeight = EPD47_HEIGHT;
static constexpr uint16_t LogicalWidth = EPD47_LOGICAL_WIDTH;
static constexpr uint16_t LogicalHeight = EPD47_LOGICAL_HEIGHT;

static constexpr uint8_t I2cSda = EPD47_I2C_SDA;
static constexpr uint8_t I2cScl = EPD47_I2C_SCL;
static constexpr uint32_t I2cFreq = EPD47_I2C_FREQ;
static constexpr uint8_t RtcAddress = 0x51;

static constexpr uint8_t SdCs = EPD47_SD_CS;
static constexpr uint8_t PowerButton = EPD47_BUTTON;
static constexpr uint8_t TouchInterrupt = EPD47_TOUCH_INT;
}  // namespace BoardEPD47Pins

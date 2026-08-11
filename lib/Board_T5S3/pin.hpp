#pragma once

#include <Arduino.h>

// LilyGO T5S3-4.7-e-paper-PRO / Lite
// https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO

// MCU: ESP32-S3-WROOM-1
// Flash / PSRAM: 16M / 8M
// E-paper driver: ED047TC1, 4.7 inch, 960x540, 16 gray
// Battery chips: BQ25896 (0x6B), BQ27220 (0x55)
// Touch: GT911 (0x5D)
// RTC: PCF85063 (0x51)
// E-link power driver: TPS65185 (0x68)
// IO expander: PCA9535PW (0x20)

#define T5S3_WIDTH 960
#define T5S3_HEIGHT 540
#define T5S3_LOGICAL_WIDTH 540
#define T5S3_LOGICAL_HEIGHT 960

// I2C addresses of onboard peripherals.
#define T5S3_BQ25896_ADDR 0x6B
#define T5S3_BQ27220_ADDR 0x55
#define T5S3_GT911_ADDR 0x5D
#define T5S3_PCF85063_ADDR 0x51
#define T5S3_TPS65185_ADDR 0x68
#define T5S3_PCA9535_ADDR 0x20

// Board pins.
#define T5S3_GPS_RXD 44
#define T5S3_GPS_TXD 43
#define T5S3_SerialMon Serial
#define T5S3_SerialGPS Serial2

#define T5S3_I2C_PORT 0
#define T5S3_SCL 40
#define T5S3_SDA 39
#define T5S3_I2C_FREQ 400000

#define T5S3_SPI_MISO 21
#define T5S3_SPI_MOSI 13
#define T5S3_SPI_SCLK 14

// GT911 touch.
#define T5S3_TOUCH_SCL (T5S3_SCL)
#define T5S3_TOUCH_SDA (T5S3_SDA)
#define T5S3_TOUCH_INT 3
#define T5S3_TOUCH_RST 9

// PCF8563 RTC.
#define T5S3_RTC_SCL (T5S3_SCL)
#define T5S3_RTC_SDA (T5S3_SDA)
#define T5S3_RTC_IRQ 2

// SD card.
#define T5S3_SD_MISO (T5S3_SPI_MISO)
#define T5S3_SD_MOSI (T5S3_SPI_MOSI)
#define T5S3_SD_SCLK (T5S3_SPI_SCLK)
#define T5S3_SD_CS 12

// LoRa SX1262.
#define T5S3_LORA_MISO (T5S3_SPI_MISO)
#define T5S3_LORA_MOSI (T5S3_SPI_MOSI)
#define T5S3_LORA_SCLK (T5S3_SPI_SCLK)
#define T5S3_LORA_CS 46
#define T5S3_LORA_IRQ 10
#define T5S3_LORA_RST 1
#define T5S3_LORA_BUSY 47

// Misc.
#define T5S3_BL_EN 11
#define T5S3_PCA9535_INT 38
#define T5S3_BOOT_BTN 0

// ED047TC1 e-paper.
#define EP_SCL 40
#define EP_SDA 39
#define EP_INTR 38
#ifdef ESP_IDF_VERSION_MAJOR
#define EP_I2C_PORT I2C_NUM_0
#endif

#define EP_D15 47
#define EP_D14 21
#define EP_D13 14
#define EP_D12 13
#define EP_D11 12
#define EP_D10 11
#define EP_D9 10
#define EP_D8 9
#define EP_D7 8
#define EP_D6 18
#define EP_D5 17
#define EP_D4 16
#define EP_D3 15
#define EP_D2 7
#define EP_D1 6
#define EP_D0 5
#define EP_CKV 48
#define EP_STH 41
#define EP_LEH 42
#define EP_STV 45
#define EP_CKH 4

// PCA9535 linear IO indexes, matching IO0..IO15.
// IO1x maps to port 1 bit x, so IO10 is linear index 8.
#define PCA9535_IO10_EP_OE 8
#define PCA9535_IO11_EP_MODE 9
#define PCA9535_IO12_BUTTON 10
#define PCA9535_IO13_TPS_PWRUP 11
#define PCA9535_IO14_VCOM_CTRL 12
#define PCA9535_IO15_TPS_WAKEUP 13
#define PCA9535_IO16_TPS_PWR_GOOD 14
#define PCA9535_IO17_TPS_INT 15

// IO0x maps to port 0 bit x.
#define PCA9535_IO00_LORA_GPS_EN 0
#define PCA9535_IO01 1
#define PCA9535_IO02 2
#define PCA9535_IO03 3
#define PCA9535_IO04 4
#define PCA9535_IO05 5
#define PCA9535_IO06 6
#define PCA9535_IO07 7

namespace BoardT5S3Pins {
static constexpr uint16_t DisplayWidth = T5S3_WIDTH;
static constexpr uint16_t DisplayHeight = T5S3_HEIGHT;
static constexpr uint16_t LogicalWidth = T5S3_LOGICAL_WIDTH;
static constexpr uint16_t LogicalHeight = T5S3_LOGICAL_HEIGHT;

static constexpr uint8_t I2cSda = T5S3_SDA;
static constexpr uint8_t I2cScl = T5S3_SCL;
static constexpr uint32_t I2cFreq = T5S3_I2C_FREQ;
static constexpr uint8_t RtcAddress = T5S3_PCF85063_ADDR;

static constexpr uint8_t SdCs = T5S3_SD_CS;
static constexpr uint8_t LoraCs = T5S3_LORA_CS;
static constexpr uint8_t LoraGpsEnable = PCA9535_IO00_LORA_GPS_EN;
static constexpr uint8_t ExpanderButton = PCA9535_IO12_BUTTON;
static constexpr uint8_t BootButton = T5S3_BOOT_BTN;
static constexpr uint8_t PowerButton = T5S3_BOOT_BTN;
static constexpr uint8_t TouchInterrupt = T5S3_TOUCH_INT;
}  // namespace BoardT5S3Pins

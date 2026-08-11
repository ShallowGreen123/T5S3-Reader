# T5S3 / EPD47 Reader

[![PlatformIO Build](https://github.com/ShallowGreen123/t5s3-reader/actions/workflows/platformio-build.yml/badge.svg)](https://github.com/ShallowGreen123/t5s3-reader/actions/workflows/platformio-build.yml)

English | [中文](README_CN.md)

Firmware for the **LilyGo T5S3** and **LilyGo EPD47 ESP32-S3** 4.7-inch
e-paper devices.

This project is adapted from CrossPoint Reader. Board support is selected at
compile time so each firmware image targets exactly one hardware platform.

## Thanks

Special thanks to [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). This firmware keeps and builds on CrossPoint's activity-based UI architecture, reader logic, settings system, SD-card cache, web file transfer, and many other foundations.

This repository is not the official CrossPoint project and is not affiliated with LilyGo. It is an adaptation and experimental firmware for supported LilyGo e-paper devices.

## Target Devices

| Build target | Hardware | Status |
| --- | --- | --- |
| `t5s3-pro` | [LilyGo T5 ePaper S3](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO) | Existing target |
| `lilygo-epd47-s3` | [LilyGo EPD47 ESP32-S3](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47/tree/esp32s3) | Compiles; physical acceptance testing is pending |

Both targets use an ESP32-S3, a 960 x 540 panel, a 540 x 960 default portrait
layout, GT911 touch, and microSD storage. The older ESP32-WROVER EPD47 is not
supported. EPD47 has no front light or detailed BQ battery telemetry, uses
GPIO21 for wake, and cannot wake from touch.

| ![](./docs/README_img/t5s3.png) | ![](./docs/README_img/t5s31.png) |
| --- | --- |

## Features

- EPUB reading with chapter parsing, layout, saved progress, and image support.
- TXT / Markdown reading.
- XTC reading.
- BMP image viewer.
- Recent books, file browser, reading cache, cover images, and sleep screen images.
- Wi-Fi file upload and web-based file management.
- Configurable fonts, font size, line spacing, margins, orientation, and refresh mode.
- Auto power-off after long inactivity when USB is not connected.
- Reader screenshots saved to the SD card under `screenshots/`.

## Requirements

- A supported LilyGo T5S3 or EPD47 ESP32-S3 device
- microSD card
- USB-C data cable
- Python 3
- PlatformIO Core, or VS Code with the PlatformIO extension

Install PlatformIO Core:

```bash
python -m pip install platformio==6.1.19
```

Clone the repository and enter the project directory:

```bash
git clone <repository-url>
cd z-T5S3-Reader
```

## Download Firmware To The Device

### Option 1: LILYGO Spark, Recommended

1. Download and open [LILYGO Spark](https://lilygo.cc/en-us/pages/lilygo-spark?srsltid=AfmBOoorTB7ptFu2LQNLRnoI2SA0zBGJTN6JpI9J3hmHEkKhBQSmeu0Y).
2. Search for your device and install the `corsspoint_lilygo_t5s3_e_paper` firmware.

This option currently applies to T5S3 only.

![LILYGO Spark firmware](./docs/README_img/lilygo_spark.png)

### Option 2: PlatformIO

1. Connect the device to your computer with USB-C.
2. Build the firmware for the connected board:

```bash
pio run -e t5s3-pro
# or
pio run -e lilygo-epd47-s3
```

3. Upload it to the device:

```bash
pio run -e t5s3-pro -t upload
# or
pio run -e lilygo-epd47-s3 -t upload
```

4. If upload mode is not detected, hold the BOOT button and press RESET, or hold BOOT while reconnecting USB, then run the upload command again.

5. Open the serial monitor if logs are needed:

```bash
pio device monitor -b 115200
```

### Option 3: Flash Download Tool

1. Download [Flash Download Tool](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html).

2. Select `esp32s3`.

![](./docs/README_img/download1.png)

3. Select a complete merged image intended for the exact board, set the flash
   address specified with that image, choose your serial port, and click
   `START`. PlatformIO's `.pio/build/<environment>/firmware.bin` and the CI
   `firmware-<board-id>.bin` artifacts are application images, not merged
   address-`0x0` recovery images.

![](./docs/README_img/download2.png)

## Firmware Update Safety

Release and CI artifacts use board-qualified names:

- `firmware-t5s3-pro.bin`
- `firmware-lilygo-epd47-s3.bin`

OTA checks select only the asset for the current board. SD-card updates also
inspect an embedded board marker and reject firmware built for the other board.
Use PlatformIO upload to recover from a failed or interrupted application
update: hold the board's BOOT button, press RESET (or reconnect USB), release
BOOT, and upload the correct environment again.

The EPD47 target links the GPL-3.0 LilyGo display driver. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before distributing an EPD47
binary.

## SD Card And Books

Put books directly in the SD card root directory, or organize them into folders.

Recommended layout:

```text
/
  Books/
    book.epub
    novel.txt
  .sleep/
    sleep.bmp
```

The firmware creates a `.crosspoint/` directory on the SD card for settings, reading progress, cache, and cover thumbnails. If cache corruption or repeated crashes occur, back up the SD card and delete `.crosspoint/` to let the firmware regenerate it.

### How To Add Fonts

If you already have prebuilt `.cpfont` files, copy them into the SD card `.fonts/` directory, insert the SD card into the device, and then select the font in `Settings -> Reader -> Reader Font Family`.

`SourceHanSansSC` is the Chinese-capable font family currently prepared in this repository.

![](./docs/README_img/fonts1.png)

More information:

- English font generation reference: [sd-card-fonts](./docs/sd-card-fonts.md)
- Chinese font usage guide: [Chinese Font Usage Guide](./docs/Chinese%20Font%20Usage%20Guide.md)

## Device Operation

### Basic Buttons

| Button | Function |
| --- | --- |
| BOOT | Short press: previous item / previous page |
| IO48 | Short press: next item / next page |
| BOOT | Long press: confirm / open |
| IO48 | Long press: power off |
| PWR | Turn on device power |
| RTS | Reset |
| HOME | Return to home screen |

### Power

- Long press `PWR` to turn on the device.
- Long press `IO48` to power off.
- When there is no activity for a long time and USB is not connected, the device enters power-off / low-power state automatically.
- If the device stops responding, press RESET and then long press the power button again.

### Home Screen

The home screen provides:

- Continue Reading: reopen the most recent book.
- Browse Files: browse files on the SD card.
- Recent Books: view recently opened books.
- File Transfer: upload books over Wi-Fi.
- Settings: configure the device.

Use Left/Right or Up/Down to move, Confirm to open, and Back to return.

### File Browser

- Left / Up: move up.
- Right / Down: move down.
- Confirm: open a file or folder.
- Back: go to the parent folder or return home.
- Long press Confirm: delete the selected file after confirmation.

### Reading

- Right or Down: next page.
- Left or Up: previous page.
- Confirm: open the reader menu.
- Back: exit reading and return home.
- Long press Back: exit reading and return to the file browser.
- Long press page keys: chapter skip or other configured long-press behavior.
- Power + Down: take a screenshot and save it under `screenshots/` on the SD card.

### Wi-Fi Book Upload

1. Open `File Transfer` from the home screen.
2. Select and connect to Wi-Fi.
3. The device displays a web address.
4. Open the address in a browser on your computer or phone.
5. Upload EPUB, TXT, or other supported files to the SD card.
6. Press Back on the device to exit file transfer mode.

## Common Settings

In `Settings`, you can configure:

- Backlight level from 0 to 10. `0` turns it off, the default is `2`, it automatically turns off during sleep or power-off, and the saved level is restored after wake or boot.
- Font, font size, line spacing, and page margins.
- Reading orientation: portrait, landscape, inverted, and more.
- Refresh mode: quality, balanced, or fast.
- EPUB image rendering: show images, placeholders, or hide images.
- Sleep / power-off timeout.
- Sleep screen: default image, blank screen, custom BMP, or book cover.
- Button mapping.
- Wi-Fi networks.

## Notes

This firmware is still being tuned. E-paper refresh, image decoding, large TXT loading, power consumption, and battery reporting can vary with hardware state. If something goes wrong, please provide serial logs, reproduction files, and exact steps when possible.

Thanks again to CrossPoint Reader and all related open-source library authors.

# LilyGo-EPD47 Adaptation Execution Plan

## Goal

Add a compile-time selectable LilyGo-EPD47 ESP32-S3 target while preserving the existing
T5S3-4.7-e-paper-PRO/Lite firmware behavior.

Supported build targets:

```bash
pio run -e t5s3-pro
pio run -e lilygo-epd47-s3
```

The initial EPD47 target is the ESP32-S3, 16 MB flash, 8 MB OPI PSRAM board described by
the `esp32s3` branch of `Xinyuan-LilyGO/LilyGo-EPD47`. The older ESP32-WROVER board is
out of scope.

## Constraints and Decisions

- Board selection is compile-time. A firmware image supports exactly one board.
- Reader, rendering, storage API, activities, settings, and network code remain shared.
- The existing T5S3 display backend remains based on M5GFX.
- The EPD47 display backend uses the vendor driver pinned to a reviewed commit because
  the current M5GFX `Bus_EPD` assumes direct GPIO access to control signals that EPD47
  routes through a 74HCT4094 shift register.
- The vendor repository is GPL-3.0. The project source remains under its existing license,
  but a distributed EPD47 firmware image that links the vendor driver must comply with
  GPL-3.0 and include the corresponding source and notices. The T5S3 build does not link
  this dependency.
- EPD47 touch wake is disabled by default. GPIO47 is not a deep-sleep-capable RTC GPIO on
  ESP32-S3. GPIO21 is the default wake button.
- Unsupported features are exposed through board capabilities rather than silently
  pretending the hardware exists.

## Hardware Matrix

| Function | T5S3 PRO/Lite | LilyGo-EPD47-S3 |
| --- | --- | --- |
| Display | ED047TC1, 960x540 | ED047TC1, 960x540 |
| Display power/control | PCA9535 + TPS65185 | 74HCT4094 configuration register |
| SD SPI | SCLK14, MISO21, MOSI13, CS12 | SCLK11, MISO16, MOSI15, CS42 |
| I2C | SDA39, SCL40 | SDA18, SCL17 |
| Touch | GT911, IRQ3, RESET9 | GT911, IRQ47, RESET pulled high in hardware |
| Battery | BQ25896 + BQ27220 | ADC GPIO14 |
| Front light | GPIO11 PWM | Not available |
| Wake button | GPIO0 | GPIO21 |

## Work Breakdown

### 1. Build Configuration

- [x] Add unambiguous PlatformIO board definitions for both devices.
- [x] Add `t5s3-pro` and `lilygo-epd47-s3` environments.
- [x] Keep board-specific dependencies and macros isolated per environment.
- [x] Build both environments in CI.

### 2. Board Abstraction

- [x] Add a compile-time `Board` facade and shared board data types.
- [x] Move application and HAL users from `BoardT5S3` to the facade.
- [x] Define capabilities for front light, detailed battery telemetry, hard power-off,
      touch wake, RTC, and touch.
- [x] Preserve the existing T5S3 implementation without behavioral changes.

### 3. EPD47 Board Support

- [x] Implement I2C locking and initialization.
- [x] Implement SD SPI initialization with the EPD47 pin map.
- [x] Implement GT911 probing at addresses `0x14` and `0x5D` without a software reset pin.
- [x] Implement GPIO21 button input and deep-sleep wake.
- [x] Implement calibrated GPIO14 battery voltage and percentage estimation.
- [x] Implement sleep pin cleanup and unsupported-feature fallbacks.

### 4. EPD47 Display Backend

- [x] Keep the shared 1-bpp framebuffer and public `HalDisplay` API.
- [x] Initialize the vendor ED047TC1 driver only in the EPD47 build.
- [x] Convert the shared grayscale planes to the driver's packed 4-bpp format.
- [x] Map full, half, balanced, and fast refresh requests to safe EPD47 update sequences.
- [x] Power the panel down after refresh and before deep sleep.
- [x] Document that waveform quality and timing require hardware validation.

### 5. Capability-Aware Application Behavior

- [x] Hide front-light settings on EPD47.
- [x] Replace direct T5S3 battery types with shared types.
- [x] Show basic ADC battery information on EPD47 instead of BQ chip diagnostics.
- [x] Fall back to deep sleep when hard PMIC shutdown is unavailable.
- [x] Map a short GPIO21 press to Back and a two-second press to deep-sleep power-off.
- [x] Report the selected board ID in logs and the web status endpoint.

### EPD47 Controls

- Short press GPIO21: Back. In a reader page this returns to the home page; in settings
  and other sub-pages it returns to the previous page.
- Hold GPIO21 for two seconds: save application state, show the power-off screen, and
  enter deep sleep. EPD47 has no software-controlled battery disconnect, so deep sleep
  is its power-off state.
- While powered off, press GPIO21 once to wake and boot. Touch wake is not available on
  this ESP32-S3 pin assignment.

### 6. Firmware Safety and Release

- [x] Produce board-qualified firmware names.
- [x] Make OTA release asset selection board-aware.
- [x] Add a board identity check before SD/OTA installation where practical.
- [x] Document flashing and recovery steps for both boards.

## Verification Gates

### Automated

- [x] `pio run -e t5s3-pro`
- [x] `pio run -e lilygo-epd47-s3`
- [x] Existing host tests.
- [ ] CI matrix builds both board targets.

### Hardware: T5S3 Regression

- [ ] Cold boot and home screen.
- [ ] SD card and book opening.
- [ ] Touch, buttons, front light, battery, sleep, and wake.
- [ ] Full, balanced, and fast refresh behavior.

### Hardware: EPD47 Acceptance

- [ ] White, black, checkerboard, text, and grayscale test screens.
- [ ] Correct portrait geometry and touch coordinates.
- [ ] SD card read/write and EPUB/TXT opening.
- [ ] GPIO21 input and wake from deep sleep.
- [ ] RTC detection and Wi-Fi/web server operation.
- [ ] Battery voltage sanity against a multimeter.
- [ ] Thirty sleep/wake cycles without display corruption.
- [ ] Refresh ghosting and measured sleep current recorded.

## Completion Rule

Automated completion means that both targets compile and shared tests pass. The adaptation is
not considered hardware-complete until the EPD47 hardware checklist is executed on a physical
board. Any unverified hardware item remains explicitly marked as pending.

## Automated Verification Results

Verified locally on 2026-08-10:

| Target / test | Result |
| --- | --- |
| `t5s3-pro` | Success; RAM 59,416 B, flash 5,640,285 B |
| `lilygo-epd47-s3` | Success; RAM 60,312 B, flash 5,602,909 B |
| Release JSON parser | 33 passed, 0 failed |
| Streaming JSON parser | 30 passed, 0 failed |
| Differential rounding | 9 passed, 0 failed |

Both generated application images contain only their expected
`CROSSPOINT_BOARD_ID:<board-id>` marker. The CI workflow syntax was parsed
locally; its verification gate remains pending until the workflow runs on
GitHub Actions. All physical-hardware gates remain pending.

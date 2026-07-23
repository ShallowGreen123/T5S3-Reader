"""
PlatformIO post-build script: produce a single merged flash image.

After the application `firmware.bin` is built, this combines the bootloader,
partition table, boot_app0, and the application into one `firmware-merged.bin`
that can be flashed at offset 0x0 in a single step (factory flashing, web
flashers such as esp-web-tools / esptool).

It reuses the exact images and offsets PlatformIO already computed for this
board (`FLASH_EXTRA_IMAGES` + `ESP32_APP_OFFSET`) and mirrors the uploader's
flash header args, so the merged image matches what the normal flasher writes.
"""

import os

# PlatformIO/SCons entry point — Import and env are SCons builtins injected at runtime.
Import("env")  # noqa: F821  # type: ignore[name-defined]

board = env.BoardConfig()  # noqa: F821  # type: ignore[name-defined]
mcu = board.get("build.mcu", "esp32")


def merge_firmware(source, target, env):  # noqa: F811
    merged = os.path.join(env.subst("$BUILD_DIR"), env.subst("${PROGNAME}") + "-merged.bin")

    cmd = [
        '"$PYTHONEXE"', '"$OBJCOPY"',
        "--chip", mcu, "merge_bin",
        "-o", merged,
        "--flash_mode", "${__get_board_flash_mode(__env__)}",
        "--flash_freq", "${__get_board_f_image(__env__)}",
        "--flash_size", board.get("upload.flash_size", "16MB"),
    ]

    # Bootloader, partition table and boot_app0 — offset/path pairs PlatformIO
    # already resolved for this board (keeps offsets correct without hard-coding).
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):
        cmd += [offset, env.subst(image)]

    # The application itself, at its configured offset.
    cmd += ["$ESP32_APP_OFFSET", "$BUILD_DIR/${PROGNAME}.bin"]

    env.Execute(env.VerboseAction(" ".join(cmd), "Merging firmware image -> " + merged))


# Run after the application binary is produced.
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware)  # noqa: F821  # type: ignore[name-defined]

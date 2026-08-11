# Third-Party Notices

## LilyGo-EPD47 display driver

The `lilygo-epd47-s3` build links display-driver code from
[Xinyuan-LilyGO/LilyGo-EPD47](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47),
pinned in `platformio.ini` to commit
`f0ff4a98e5cb60f98a46dbc09b511dc4534aae43`.

That upstream repository is licensed under the GNU General Public License,
version 3 (GPL-3.0). The repository's MIT license continues to apply to the
original project sources, but a distributed EPD47 firmware binary is a combined
work that includes the GPL driver and must be distributed in compliance with
GPL-3.0.

Anyone publishing `firmware-lilygo-epd47-s3.bin` must also provide the complete
corresponding source used for that build, including the pinned LilyGo-EPD47
driver source, build configuration, scripts, notices, and any local changes.
Publishing only a link to the upstream repository is not a substitute for
providing the corresponding source. The T5S3 build does not link this driver.

# T5S3 / EPD47 Reader

[![PlatformIO Build](https://github.com/ShallowGreen123/t5s3-reader/actions/workflows/platformio-build.yml/badge.svg)](https://github.com/ShallowGreen123/t5s3-reader/actions/workflows/platformio-build.yml)

[English](README.md) | 中文

适用于 **LilyGo T5S3** 与 **LilyGo EPD47 ESP32-S3** 4.7 寸墨水屏设备的电子书阅读器固件。编译时选择板型，每个固件只对应一种硬件。

本项目基于 CrossPoint Reader 的代码和设计继续改造，适配 LilyGo T5S3 与 EPD47 ESP32-S3 硬件，并针对 EPUB 插图、TXT 打开速度、低功耗关机、开机刷新等问题做了优化。

## 致谢

感谢 [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 项目。这个固件继承了 CrossPoint 的活动页面架构、阅读器逻辑、设置系统、SD 卡缓存、Web 文件传输等大量基础工作。

本仓库不是 CrossPoint 官方项目，也不隶属于 LilyGo。它是面向 T5S3 设备的适配和实验版本。

## 使用的设备

当前目标设备：

| 编译目标 | 硬件 | 状态 |
| --- | --- | --- |
| `t5s3-pro` | [LilyGo T5 ePaper S3](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO) | 原有目标 |
| `lilygo-epd47-s3` | [LilyGo EPD47 ESP32-S3](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47/tree/esp32s3) | 已通过编译，待实机验收 |

两种目标均使用 ESP32-S3、960 x 540 墨水屏、GT911 触摸与 microSD。旧版 ESP32-WROVER EPD47 不在支持范围内。EPD47 没有前光和 BQ 电量计，使用 GPIO21 唤醒，且不支持触摸唤醒。

| ![](./docs/README_img/t5s3.png) | ![](./docs/README_img/t5s31.png) |
| --- | --- |

## 功能

- 支持 EPUB 阅读，包括章节解析、排版、阅读进度和插图显示。
- 支持 TXT / Markdown 文本阅读。
- 支持 XTC 文件阅读。
- 支持 BMP 图片查看。
- 支持最近阅读、文件浏览、阅读缓存和封面/睡眠图。
- 支持 Wi-Fi 文件上传和 Web 文件管理。
- 支持字体、字号、行距、边距、屏幕方向、刷新模式等设置。
- 支持长时间无操作自动关机；插入 USB 时不会自动关机。
- 支持阅读页截图，截图保存到 SD 卡 `screenshots/` 目录。

## 准备工作

你需要：

- 受支持的 LilyGo T5S3 或 EPD47 ESP32-S3 设备
- microSD 卡
- USB-C 数据线
- Python 3
- PlatformIO Core，或 VS Code + PlatformIO 插件

安装 PlatformIO Core：

```bash
python -m pip install platformio==6.1.19
```

获取源码后进入仓库根目录：

```bash
git clone <仓库地址>
cd z-T5S3-Reader
```

## 如何下载程序到设备

### 方式一：使用 LILYGO Spark，推荐

1. 下载并打开 [LILYGO Spark](https://lilygo.cc/en-us/pages/lilygo-spark?srsltid=AfmBOoorTB7ptFu2LQNLRnoI2SA0zBGJTN6JpI9J3hmHEkKhBQSmeu0Y)。
2. 搜索你的设备，并下载 `corsspoint_lilygo_t5s3_e_paper` 程序。

该方式目前只适用于 T5S3。

![LILYGO Spark 固件下载](./docs/README_img/lilygo_spark.png)

### 方式二：使用 PlatformIO 下载

1. 将设备通过 USB-C 连接电脑。
2. 在仓库根目录编译固件：

```bash
pio run -e t5s3-pro
# 或
pio run -e lilygo-epd47-s3
```

3. 下载到设备：

```bash
pio run -e t5s3-pro -t upload
# 或
pio run -e lilygo-epd47-s3 -t upload
```

4. 如果无法进入下载模式，可以按住设备的 BOOT 键，再按一下 RESET，或按住 BOOT 后重新插入 USB，然后重新执行上传命令。

5. 如需查看串口日志：

```bash
pio device monitor -b 115200
```

### 方式三：使用 flash_download_tools 手动刷入

1. 下载 [Flash Download Tool](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html)

2. 选择 esp32s3

![](./docs/README_img/download1.png)

3. 选择与板型严格对应的完整合并镜像，按镜像说明设置下载地址，然后选择串口并点击 `START`。PlatformIO 生成的 `.pio/build/<环境>/firmware.bin` 和 CI 的 `firmware-<板型>.bin` 是应用镜像，不能作为地址 `0x0` 的完整恢复镜像使用。

![](./docs/README_img/download2.png)

## 固件升级安全

发布与 CI 产物使用板型限定名称：

- `firmware-t5s3-pro.bin`
- `firmware-lilygo-epd47-s3.bin`

OTA 只会选择当前板型对应的文件。通过 SD 卡升级时，固件还会检查内嵌的板型标记并拒绝另一种板子的固件。如果应用升级失败或中断，请按住开发板的 BOOT 键并按 RESET（或重新连接 USB），释放 BOOT 后通过 PlatformIO 上传正确的环境。

EPD47 目标链接了 GPL-3.0 的 LilyGo 显示驱动。分发 EPD47 二进制前请阅读 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## SD 卡和电子书

将电子书直接放到 SD 卡根目录，或按自己的习惯创建文件夹分类。

推荐结构：

```text
/
  Books/
    book.epub
    novel.txt
  .sleep/
    sleep.bmp
```

固件会在 SD 卡上创建 `.crosspoint/` 目录，用于保存设置、阅读进度、缓存和封面缩略图。若遇到异常缓存或反复崩溃，可以备份后删除 `.crosspoint/` 让系统重新生成。

### 如何添加字体

在 SD_fonts/ 文件夹下载有一些字体，只需要将字体文件复制到SD的 .fonts/ 文件夹下，然后在 SD 插入设备，就可以在 `Setting -> Reader -> Reader Font Family` 中设置所使用的字体；

其中 `SourceHanSansSC` 字体是包含中文的；

![](./docs/README_img/fonts1.png)

更多英文字体生成参考文档：[sd-card-fonts](./docs/sd-card-fonts.md)

更多中文字体生成参考文档：[中文字体使用说明](./docs/中文字体使用说明.md)

## 设备如何操作

### 基础按键

| 按键 | 功能 |
| --- | --- |
| BOOT | 短按：上一项 / 阅读时上一页 |
| IO48 | 短按：下一项 / 阅读时下一页 |
| BOOT | 长按：确认 / 打开 |
| IO48 | 长按：关机 |
| PWR | 打开设备电源 |
| RTS | 复位 |
| HOME | 回到主页面 |

### 开机和关机

- 长按 `PWR` 键开机。
- 长按 `IO48` 键关机。
- 长时间无操作且未插 USB 时，设备会自动进入关机/低功耗状态。
- 如果设备无响应，可以按 RESET 后重新长按电源键启动。

### 主页面

主页面可以进入：

- Continue Reading：继续阅读最近一本书。
- Browse Files：浏览 SD 卡文件。
- Recent Books：最近阅读列表。
- File Transfer：通过 Wi-Fi 上传书籍。
- Settings：设置。

使用 Left/Right 或 Up/Down 移动选择，Confirm 打开，Back 返回。

### 文件浏览

- Left / Up：向上移动。
- Right / Down：向下移动。
- Confirm：打开文件或文件夹。
- Back：返回上一级或回到主页。
- 长按 Confirm：删除选中的文件，系统会再次确认。

### 阅读页面

- Right 或 Down：下一页。
- Left 或 Up：上一页。
- Confirm：打开阅读菜单。
- Back：退出阅读并回到主页。
- 长按 Back：退出阅读并回到文件浏览。
- 长按翻页键：按设置执行章节跳转或其他长按行为。
- Power + Down：截图，保存到 SD 卡 `screenshots/` 目录。

### Wi-Fi 上传书籍

1. 在主页面进入 `File Transfer`。
2. 选择并连接 Wi-Fi。
3. 屏幕会显示一个访问地址。
4. 在电脑或手机浏览器打开该地址。
5. 上传 EPUB、TXT 等文件到 SD 卡。
6. 上传完成后，按 Back 退出文件传输模式。

## 常用设置

在 `Settings` 中可以调整：

- 背光亮度：`0` 到 `10` 档，`0` 为关闭，默认是 `2` 档；设备休眠或关机时会自动熄灭，唤醒/开机后恢复保存的档位。
- 字体、字号、行距、页边距。
- 阅读方向：竖屏、横屏、倒置等。
- 刷新模式：质量优先、平衡、快速。
- EPUB 插图显示方式：显示插图、占位、隐藏。
- 睡眠/关机时间。
- 睡眠屏幕：默认图、空白、自定义 BMP、书籍封面。
- 按键映射。
- Wi-Fi 网络。

## 说明

这个固件仍在持续调整中。墨水屏刷新、图片解码、TXT 大文件加载、功耗和电量计都和具体硬件状态有关，如果遇到异常，请尽量提供串口日志、复现文件和操作步骤。

再次感谢 CrossPoint Reader 项目和相关开源库作者。

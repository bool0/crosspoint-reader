# InkDesk

把 Xteink X4（阅星瞳 X4）墨水屏变成 **阅读器 + 桌面时钟 + 备忘录 + 相框**。

InkDesk 是 [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 的一个分支（fork），在它完整的阅读功能之上，加入了中文界面、时钟、备忘录、图片和 USB 管理。

> 非官方项目，与 Xteink（阅星瞳）及 CrossPoint Reader 项目无隶属关系。刷写固件有风险，请先备份原厂固件。

## 功能

**阅读**（来自 CrossPoint）：EPUB / TXT / XTC，书签、目录、字体与排版设置等，详见 [docs/CROSSPOINT_README.md](docs/CROSSPOINT_README.md) 和 [USER_GUIDE.md](USER_GUIDE.md)。

**InkDesk 新增：**

- **中文**
  - 完整简体中文界面
  - 内置的 Noto Sans / Noto Serif 字体会自动用 SD 卡上的中文字体（思源黑体 / 思源宋体 / 霞鹜文楷等）显示中文
  - 中文段落按“行行排满”断行，避免字间距被拉大
- **时钟**
  - 七段数码大字时钟，4 种数字样式，可选显示日期、电量、备忘录，支持深色模式
  - 只在用户要求时联网校准；也可以通过 USB 用电脑时间校准
  - **锁屏时钟**：锁屏后每分钟快速刷新一次时间，其余时间深度休眠
- **备忘录**
  - Markdown（标题、粗体、斜体、删除线、列表、引用、分隔线）
  - 清单 `- [ ]`：设备上用按键勾选，手机/电脑上点选；子项全部完成时父项自动勾选
  - 在手机或电脑网页上编辑，拖动排序备忘录和清单项
  - 可固定一条显示在时钟和锁屏上
- **图片**
  - 在网页上选择照片，自动裁剪、调整亮度对比度并抖动成 4 级灰度后发送
  - 设备上可浏览图片，也可用作锁屏 / 关机画面
- **锁屏与关机**
  - 按住电源键约 1 秒松开 = 锁屏（显示 *休眠画面*：时钟 / 备忘录 / 图片……）
  - 按住 3 秒 = 关机，彻底断电（显示 *关机画面*：图片 / 备忘录 / 默认 / 空白）
- **USB 连接**
  - 插上 USB 线自动显示「USB 连接」页面
  - 用 Chrome / Edge 双击打开 `InkDesk.html`（Web Serial，一个文件），无需安装软件即可管理文件、备忘录和图片；Windows、macOS、Linux、Android（Chrome 148+）均可使用
  - 另附一个 Python 小工具（`tools/inkdesk/usb-helper`），可在 Safari 等任何浏览器中使用

## 支持的设备

| 设备 | 状态 |
|---|---|
| Xteink X4 | 已在实机上测试 |
| 其他 CrossPoint 支持的设备 | 可以编译，但新增功能未经测试 |

## 安装

### 1. 准备工具

需要 Python 3.8+ 和 [pioarduino 版 PlatformIO](https://github.com/pioarduino/platformio-core)（标准版 PlatformIO 6.2 在此项目上会构建失败）：

```bash
python3 -m venv .venv
.venv/bin/pip install https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip esptool pyserial
git clone --recursive https://github.com/bool0/crosspoint-reader
cd crosspoint-reader
```

### 2. 备份原厂固件（强烈建议）

用 USB 线连接 X4 并按电源键唤醒，然后：

```bash
../.venv/bin/esptool --port <串口> read-flash 0 0x1000000 x4_stock_backup.bin
```

串口在 macOS 上类似 `/dev/cu.usbmodem*`，Linux 上类似 `/dev/ttyACM0`，Windows 上是 `COM3` 之类。备份文件包含原厂固件和你的个人数据，**不要上传或分享**。恢复原厂固件：`esptool --port <串口> write-flash 0 x4_stock_backup.bin`。

### 3. 编译并刷写

```bash
../.venv/bin/pio run -e default -t upload --upload-port <串口>
```

首次编译会下载工具链并重建 Arduino 核心，需要较长时间。

### 4. 安装中文字体

中文需要 SD 卡上的 `.cpfont` 字体，例如把霞鹜文楷转换后放进 SD 卡的 `/fonts/WenKai/`：

```bash
pip install freetype-py fonttools
python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
  --regular LXGWWenKai-Regular.ttf --bold LXGWWenKai-Medium.ttf \
  --intervals latin-ext,punctuation,cjk --sizes 8,10,12,14,16,18 \
  --name WenKai --output-dir WenKai/
```

然后在 **设置 → 阅读 → 阅读字体** 中选择 WenKai，在 **设置 → 系统 → 语言** 中选择 **简体中文**。

字体文件可以通过网页版的「文件」页上传，也可以用 `tools/inkdesk/push_file.py` 通过 USB 传输。

## 使用工具

| 工具 | 用途 |
|---|---|
| `tools/inkdesk/web/InkDesk.html` | InkDesk 网页版（单个文件）：复制到电脑上，插上 USB 后用 Chrome / Edge 双击打开，管理文件、备忘录、图片，见其 [README](tools/inkdesk/web/README.md) |
| `tools/inkdesk/usb-helper/x4usb.py` | 本地网页助手，Safari 等任何浏览器都能用：`python3 tools/inkdesk/usb-helper/x4usb.py` |
| `tools/inkdesk/sync_time.py` | 用电脑时间校准设备时钟；`--watch` 会在每次插入 USB 时自动校准 |
| `tools/inkdesk/push_file.py` | 通过 USB 复制文件到 SD 卡，例如 `push_file.py *.cpfont --dest /fonts/WenKai/` |
| `tools/inkdesk/dev/` | 开发用：截屏、发送调试命令 |

这些 Python 工具都通过 `tools/inkdesk/x4link.py` 连接 X4（Windows、macOS、Linux 自动找到串口），需要 `pip install pyserial`。

设备也能通过 Wi-Fi 使用：主页 → 文件传输，然后在手机浏览器打开设备显示的地址，并进入 `/memo` 或 `/images` 页面。

USB 串口命令（`CMD:LS/GET/PUT/RM/MKDIR/MV/TIME…`）的格式见 [`src/network/SerialFileTransfer.h`](src/network/SerialFileTransfer.h)。

## 开发

```bash
# 主机端单元测试
cmake -S test -B build/test && cmake --build build/test -j8 && ctest --test-dir build/test

# 修改 src/network/html 下的页面后，重新生成网页版
python3 tools/inkdesk/web/build.py
```

`tools/inkdesk/web/test/emulator.js` 是一个模拟的 X4，可以在没有设备时测试网页版。代码规范见 [CLAUDE.md](CLAUDE.md)。

## 许可证

- InkDesk 新增的代码（文件头带 `SPDX-License-Identifier: Apache-2.0`）采用 [Apache License 2.0](LICENSE)。
- 来自 CrossPoint Reader 的代码（包括 InkDesk 修改过的部分）采用 [MIT License](LICENSE-MIT)；`freeink-sdk/` 为 FreeInk SDK（MIT）。
- 第三方组件见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
- **编译出的固件**链接了 wolfSSL（GPL-2.0-or-later）等库，分发固件二进制文件时须按 GPL-3.0 提供，并附上对应的完整源代码，详见 [NOTICE](NOTICE)。

## 致谢

- [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 及其贡献者，InkDesk 的全部阅读功能都来自这个项目
- FreeInk SDK 与 OpenX4 E-Paper 社区
- [霞鹜文楷](https://github.com/lxgw/LxgwWenKai)、[Noto CJK](https://github.com/notofonts/noto-cjk) 等开源字体

---

## English

InkDesk is a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) for the Xteink X4 e-ink reader. It adds:
- a Simplified Chinese UI, CJK font fallback and CJK-aware line breaking
- a desk clock with a live lock-screen clock
- Markdown memos with nested checklists, editable from a phone or computer
- images for the gallery and the lock / power-off screens
- a hold-to-power-off power button
- USB management from Chrome / Edge via Web Serial

New code is Apache-2.0, CrossPoint-derived code stays MIT, and firmware binaries are GPL-3.0 because they link wolfSSL; see [NOTICE](NOTICE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Build with pioarduino PlatformIO: `pio run -e default -t upload`.

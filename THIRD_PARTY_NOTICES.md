# Third-party notices

InkDesk builds on the projects below. Each keeps its own licence; the full
licence texts are in the paths given, or in the library's own repository for
components that PlatformIO downloads at build time.

## Upstream code

| Component | Licence | Where |
|---|---|---|
| CrossPoint Reader | MIT | `LICENSE-MIT` |
| FreeInk SDK (includes code derived from the OpenX4 E-Paper Community SDK) | MIT | `freeink-sdk/LICENSE`, `freeink-sdk/NOTICE` |

## Libraries downloaded at build time

These are fetched by PlatformIO (`platformio.ini`) and are not stored in this
repository. They are linked into the firmware image.

| Library | Version | Licence | Notes |
|---|---|---|---|
| [wolfSSL](https://github.com/wolfSSL/Arduino-wolfSSL) | 5.7.2 | **GPL-2.0-or-later** | TLS for HTTPS features. Makes a firmware binary a GPL work (see below). |
| [Arduino-ESP32 core](https://github.com/espressif/arduino-esp32) | 3.3.x | **LGPL-2.1** | Statically linked. |
| [arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets) | 2.7.3 | **LGPL-2.1** | Statically linked. |
| [ESP-IDF](https://github.com/espressif/esp-idf) | 5.5.x | Apache-2.0 | |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | 7.4.2 | MIT | |
| [SdFat](https://github.com/greiman/SdFat) | 2.3.1 | MIT | |
| [QRCode](https://github.com/ricmoo/QRCode) | 0.0.1 | MIT | Copyright (c) 2017 Richard Moore |
| [PNGdec](https://github.com/bitbank2/PNGdec) | 1.1.6 | Apache-2.0 | Copyright 2020 BitBank Software, Inc. |
| [JPEGDEC](https://github.com/bitbank2/JPEGDEC) | commit `8628297` | Apache-2.0 | Copyright 2020 BitBank Software, Inc. |

## Code included in this repository

| Component | Licence | Where |
|---|---|---|
| Expat XML parser | MIT | `lib/expat` |
| miniz | MIT | `lib/miniz/third_party` |
| uzlib | zlib | `lib/uzlib` |
| minibidi (Ahmad Khalifa, arabeyes.org) | MIT | `lib/MiniBidi` |
| Lucide icons | ISC | `freeink-sdk/libs/assets/Icons/lucide/LICENSE` |
| stb_truetype | MIT or Public Domain | `freeink-sdk/libs/font/FreeInkFont/third_party/stb` (only built for boards with PSRAM; not in X4 firmware) |
| FreeType | FreeType License (FTL) or GPL-2.0 | `freeink-sdk/libs/font/FreeInkFont/third_party/freetype` (only built for boards with PSRAM; not in X4 firmware) |
| Hyphenation patterns | Tries from [typst/hypher](https://github.com/typst/hypher) (MIT OR Apache-2.0), compiled from the TeX [hyph-utf8](https://github.com/hyphenation/tex-hyphen) patterns; each language's original pattern licence applies | `lib/Epub/Epub/hyphenation/generated` |

## Online services

The firmware and tools call these services at run time; no data from them is
stored in this repository.

| Service | Used for | Terms |
|---|---|---|
| [Open-Meteo](https://open-meteo.com/) | Weather forecast, city search | Free API for non-commercial use only; data under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), credited as "Weather data by Open-Meteo.com" on the weather screen and page. Commercial use (for example selling devices with this firmware) needs an Open-Meteo subscription. |
| [ipwho.is](https://ipwhois.io/) | Locating the network by its IP address | Free tier without a key (1,000 requests a day per IP), commercial use allowed |
| [GeoJS](https://www.geojs.io/) | Fallback IP location | Free; data from MaxMind GeoLite |

## Fonts

Built-in fonts are converted to bitmaps and compiled into the firmware. Their
sources and licences are in `lib/EpdFont/builtinFonts/source/`.

| Font | Licence |
|---|---|
| Noto Sans, Noto Serif, Noto Sans Arabic, Noto Sans Hebrew | SIL Open Font License 1.1 |
| OpenDyslexic | SIL Open Font License 1.1 (Reserved Font Name "OpenDyslexic") |
| Ubuntu | Ubuntu Font Licence 1.0 |

The Chinese SD-card fonts used with InkDesk are **not** part of this repository.
If you redistribute converted `.cpfont` files, include each font's licence:

| Font | Licence |
|---|---|
| LXGW WenKai (霞鹜文楷) | SIL Open Font License 1.1 |
| Noto Sans SC / Noto Serif SC (思源黑体 / 思源宋体) | SIL Open Font License 1.1 |

Do not add proprietary fonts (for example Microsoft YaHei / 微软雅黑) to the
repository or to published SD-card images.

## Firmware binaries and the GPL

The source code in this repository can be used under its own licences (see
`NOTICE`). A **compiled firmware image** additionally contains wolfSSL
(GPL-2.0-or-later) and LGPL-2.1 libraries, so whoever distributes a firmware
binary must:

1. distribute it under the GNU General Public License, version 3 (the
   Apache-2.0 parts are compatible with GPL-3.0; they are not compatible with
   GPL-2.0-only, which is why version 3 is used);
2. provide the complete corresponding source code: this repository at the
   commit the binary was built from, including the `freeink-sdk` submodule,
   plus the build instructions in `README.md`;
3. ship or link the GPL-3.0 and LGPL-2.1 licence texts with the binary.

Linking to the tagged source in the release notes satisfies item 2 for GitHub
Releases.

# X4 网页版 (X4 Web)

在浏览器里通过 **USB 线** 管理 X4 的文件和备忘录，电脑上不用装任何软件。

## 支持的浏览器

| 系统 | 浏览器 | 备注 |
|---|---|---|
| Windows 10/11 | Chrome、Edge | 直接可用 |
| macOS | Chrome、Edge | 直接可用（Safari 不支持） |
| Linux | Chrome、Chromium、Edge | 需要串口权限：`sudo usermod -aG dialout $USER`，重新登录 |
| Android | Chrome 148 及以上 | 用 USB-C 线（或 OTG 转接头）连接手机和 X4 |
| iPhone / iPad | — | iOS 浏览器不支持网页访问 USB 设备 |

Firefox 和 Safari 不支持 Web Serial。

## 最简单的用法：InkDesk.html

把本文件夹里的 **`InkDesk.html`**（一个文件，包含文件、备忘录、图片三个页面）复制到电脑桌面。以后每次用 USB 线连接 X4 后，双击它（用 Chrome 或 Edge 打开），点 **连接 X4** 即可。不用装软件，也不用联网。

## 使用

1. 用 USB 线连接 X4 并唤醒，X4 会自动显示「USB 连接」页面（也可以从主页进入）。
2. 用 Chrome / Edge 打开 `InkDesk.html`。
3. 点页面顶部的 **连接 X4**，在弹出的列表里选择 X4（显示为 “USB JTAG/serial debug unit”）。
4. 「文件」页可浏览、上传（把文件拖进页面）、下载、重命名、删除；「备忘录」页可编辑备忘录、勾选清单、拖动排序。

同一台电脑第二次打开时会自动连接，不用再选。

## 放到网上（可选）

`InkDesk.html` 也可以放到任意 **https** 静态网站（例如 GitHub Pages）。用 Chrome 打开一次并连接 X4 后，X4 会记住这个网址，「USB 连接」页面就会显示它的二维码。

## 开发

- `InkDesk.html` 由 `python3 build.py` 生成：备忘录页和图片页来自固件里的 `src/network/html/MemoPage.html`、`ImagesPage.html`，文件页来自 `../usb-helper/files.html`，再加上 `x4serial.js`。修改这些文件后重新运行。
- `x4serial.js` 实现与固件 `SerialFileTransfer` 相同的串口协议，并接管 `fetch()`，让为 Wi-Fi 服务器写的页面原样通过 USB 工作。
- `test/emulator.js` 是一个模拟的 X4，可在没有设备时测试网页。

<!-- SPDX-License-Identifier: Apache-2.0 | Copyright 2026 The InkDesk Authors -->

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
"""Builds InkDesk.html, the X4 browser app, next to this script.

One self-contained file with three pages and the USB transport (x4serial.js):
the firmware's own memo and images pages (src/network/html), so they never
drift apart, and the file manager (usb-helper/files.html). Copy it to a
computer and double-click it. Run again after editing any of those sources.
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
HTML_DIR = os.path.join(REPO, "src", "network", "html")
MEMO_SRC = os.path.join(HTML_DIR, "MemoPage.html")
IMAGES_SRC = os.path.join(HTML_DIR, "ImagesPage.html")
WEATHER_SRC = os.path.join(HTML_DIR, "WeatherPage.html")
FILES_SRC = os.path.join(REPO, "tools", "inkdesk", "usb-helper", "files.html")

NAV_RE = re.compile(r'<div class="nav-links">.*?</div>', re.S)


def nav(active):
    on = lambda page: ' class="active"' if active == page else ""  # noqa: E731
    return (f'<div class="nav-links">\n      <a href="#files"{on("files")} data-i18n="navFiles">Files</a>\n'
            f'      <a href="#memo"{on("memos")} data-i18n="navMemos">Memos</a>\n'
            f'      <a href="#images"{on("images")} data-i18n="navImages">Images</a>\n'
            f'      <a href="#weather"{on("weather")} data-i18n="navWeather">Weather</a>\n    </div>')


def inject_transport(html):
    return html.replace("</title>", "</title>\n    <script src=\"x4serial.js\"></script>", 1)


def build_memo():
    html = open(MEMO_SRC, encoding="utf-8").read()
    html = NAV_RE.sub(nav("memos"), html, count=1)
    html = inject_transport(html)
    html = html.replace("<title>CrossPoint Memos</title>", "<title>X4 备忘录</title>", 1)
    # Before the X4 is connected there is nothing to load; x4-connected loads it.
    html = html.replace("      refreshList().then(() => {", "      if (x4.connected) refreshList().then(() => {", 1)
    # Reload once the X4 is connected (the first load may run before it is).
    reload = """
    <script>
      window.addEventListener('x4-connected', async () => {
        await refreshList();
        if (!current && memos.length) openMemo(pinned || memos[0]);
      });
    </script>
  </body>"""
    html = html.replace("</body>", reload.lstrip("\n"), 1)
    return html


def build_images():
    html = open(IMAGES_SRC, encoding="utf-8").read()
    html = NAV_RE.sub(nav("images"), html, count=1)
    html = inject_transport(html)
    html = html.replace("<title>CrossPoint Images</title>", "<title>X4 图片</title>", 1)
    html = html.replace("      loadMode();\n      loadGrid();\n    </script>",
                        "      if (x4.connected) { loadMode(); loadGrid(); }\n    </script>", 1)
    reload = """
    <script>
      window.addEventListener('x4-connected', () => { loadMode(); loadGrid(); });
    </script>
  </body>"""
    return html.replace("</body>", reload.lstrip("\n"), 1)


def build_weather():
    html = open(WEATHER_SRC, encoding="utf-8").read()
    html = NAV_RE.sub(nav("weather"), html, count=1)
    html = inject_transport(html)
    html = html.replace("<title>CrossPoint Weather</title>", "<title>X4 天气</title>", 1)
    html = html.replace("      showNow();\n      showMode();\n    </script>",
                        "      if (x4.connected) { showNow(); showMode(); }\n"
                        "      window.addEventListener('x4-connected', () => { showNow(); showMode(); });\n"
                        "      window.addEventListener('x4-weather', showNow);\n    </script>", 1)
    return html


def build_files():
    html = open(FILES_SRC, encoding="utf-8").read()
    html = NAV_RE.sub(nav("files"), html, count=1)
    html = inject_transport(html)
    # The connection bar replaces the helper's status pill and its polling.
    html = html.replace(' <span id="conn" class="pill"></span>', "", 1)
    html = re.sub(r"\s*// Connection indicator;.*?poll\(\);\n", "\n", html, count=1, flags=re.S)
    html = html.replace("      load();\n    </script>", """      window.addEventListener('x4-connected', load);
      x4.addEventListener('change', () => { if (!x4.connected) { rowsEl.innerHTML = ''; setStatus(''); } });
      if (x4.connected) load();
    </script>""", 1)
    # Downloads go through fetch() (the USB transport), not a plain link.
    html = html.replace("""        if (f.isDirectory) a.onclick = () => go(full);
        else a.href = '/download?path=' + encodeURIComponent(full);""", """        if (f.isDirectory) a.onclick = () => go(full);
        else a.onclick = () => download(full, f.name);""", 1)
    html = html.replace("""btn('⬇', t('download'), 'secondary', () => { location.href = '/download?path=' + encodeURIComponent(full); });""",
                        """btn('⬇', t('download'), 'secondary', () => download(full, f.name));""", 1)
    html = html.replace("      async function upload(files) {", """      async function download(full, name) {
        setStatus(t('loading'));
        try {
          const blob = await (await check(await fetch('/download?path=' + encodeURIComponent(full)))).blob();
          const a = document.createElement('a');
          a.href = URL.createObjectURL(blob);
          a.download = name;
          a.click();
          setTimeout(() => URL.revokeObjectURL(a.href), 5000);
          setStatus('');
        } catch (e) {
          setStatus(t('failed') + e.message, true);
        }
      }

      // Byte progress of the current transfer, from the USB transport.
      window.addEventListener('x4-progress', (e) => {
        progressEl.hidden = false;
        progressEl.firstElementChild.style.width = (100 * e.detail.done / Math.max(1, e.detail.total)) + '%';
      });

      async function upload(files) {""", 1)
    # Keep the page's own file name: a file:// page cannot move to "/".
    html = html.replace("""history.replaceState(null, '', path === '/' ? '/' : '/?path=' + encodeURIComponent(path));""",
                        """history.replaceState(null, '', location.pathname + (path === '/' ? '' : '?path=' + encodeURIComponent(path)) + location.hash);""", 1)
    html = html.replace("<title>X4 USB 助手</title>", "<title>X4 网页版</title>", 1)
    html = html.replace("title: '🔌 X4 USB Helper'", "title: '📚 X4 Web'", 1)
    html = html.replace("title: '🔌 X4 USB 助手'", "title: '📚 X4 网页版'", 1)
    html = html.replace('data-i18n="title">🔌 X4 USB 助手', 'data-i18n="title">📚 X4 网页版', 1)
    return html


def build_bundle(pages):
    """One self-contained file: each page is a string, picked by the URL hash
    (#files, #memo, #images) and written in while the file loads. Switching
    pages reloads the file, so every page starts in a fresh window."""
    transport = open(os.path.join(HERE, "x4serial.js"), encoding="utf-8").read()
    inlined = {}
    for key, html in pages.items():
        inlined[key] = html.replace('<script src="x4serial.js"></script>', "<script>\n" + transport + "\n</script>", 1)
    data = json.dumps(inlined, ensure_ascii=False).replace("</", "<\\/").replace("<!--", "<\\!--")
    return f"""<!DOCTYPE html>
<!-- InkDesk: manage the X4 over USB. Plug in the X4, then open this file in Chrome or Edge. -->
<!-- SPDX-License-Identifier: Apache-2.0 | Copyright 2026 The InkDesk Authors | built by tools/inkdesk/web/build.py -->
<meta charset="utf-8">
<script>
  const PAGES = {data};
  const page = location.hash.slice(1) in PAGES ? location.hash.slice(1) : 'files';
  window.addEventListener('hashchange', () => location.reload());
  document.write(PAGES[page]);
</script>
"""


def main():
    pages = {"memo": build_memo(), "images": build_images(), "weather": build_weather(), "files": build_files()}
    with open(os.path.join(HERE, "InkDesk.html"), "w", encoding="utf-8") as f:
        f.write(build_bundle(pages))
    print("wrote InkDesk.html")


if __name__ == "__main__":
    main()

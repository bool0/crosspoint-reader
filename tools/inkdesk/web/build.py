#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
"""Builds the X4 browser app pages next to this script.

memo.html is the firmware's own memo page (MemoPage.html) with the USB
transport injected, so the two never drift apart. index.html is the file
manager. Run again after editing either source page.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
HTML_DIR = os.path.join(REPO, "src", "network", "html")
MEMO_SRC = os.path.join(HTML_DIR, "MemoPage.html")
IMAGES_SRC = os.path.join(HTML_DIR, "ImagesPage.html")
FILES_SRC = os.path.join(REPO, "tools", "inkdesk", "usb-helper", "files.html")

NAV_RE = re.compile(r'<div class="nav-links">.*?</div>', re.S)


def nav(active):
    on = lambda page: ' class="active"' if active == page else ""  # noqa: E731
    return (f'<div class="nav-links">\n      <a href="index.html"{on("files")} data-i18n="navFiles">Files</a>\n'
            f'      <a href="memo.html"{on("memos")} data-i18n="navMemos">Memos</a>\n'
            f'      <a href="images.html"{on("images")} data-i18n="navImages">Images</a>\n    </div>')


def inject_transport(html):
    return html.replace("</title>", "</title>\n    <script src=\"x4serial.js\"></script>", 1)


def build_memo():
    html = open(MEMO_SRC, encoding="utf-8").read()
    html = NAV_RE.sub(nav("memos"), html, count=1)
    html = inject_transport(html)
    html = html.replace("<title>CrossPoint Memos</title>", "<title>X4 备忘录</title>", 1)
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
    reload = """
    <script>
      window.addEventListener('x4-connected', () => { loadMode(); loadGrid(); });
    </script>
  </body>"""
    return html.replace("</body>", reload.lstrip("\n"), 1)


def build_files():
    html = open(FILES_SRC, encoding="utf-8").read()
    html = NAV_RE.sub(nav("files"), html, count=1)
    html = inject_transport(html)
    # The connection bar replaces the helper's status pill and its polling.
    html = html.replace(' <span id="conn" class="pill"></span>', "", 1)
    html = re.sub(r"\s*// Connection indicator;.*?poll\(\);\n", "\n", html, count=1, flags=re.S)
    html = html.replace("      load();\n    </script>", """      window.addEventListener('x4-connected', load);
      x4.addEventListener('change', () => { if (!x4.connected) { rowsEl.innerHTML = ''; setStatus(''); } });
      load();
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
    html = html.replace("<title>X4 USB 助手</title>", "<title>X4 网页版</title>", 1)
    html = html.replace("title: '🔌 X4 USB Helper'", "title: '📚 X4 Web'", 1)
    html = html.replace("title: '🔌 X4 USB 助手'", "title: '📚 X4 网页版'", 1)
    html = html.replace('data-i18n="title">🔌 X4 USB 助手', 'data-i18n="title">📚 X4 网页版', 1)
    return html


def main():
    for name, content in (("memo.html", build_memo()), ("images.html", build_images()), ("index.html", build_files())):
        with open(os.path.join(HERE, name), "w", encoding="utf-8") as f:
            f.write(content)
        print("wrote", name)


if __name__ == "__main__":
    main()

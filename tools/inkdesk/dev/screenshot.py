#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
"""Save the X4 screen as an image: screenshot.py [out.png] [--landscape]. Needs Pillow."""
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from x4link import X4  # noqa: E402

data = X4().screenshot()
img = Image.frombytes("1", (800, 480), data)  # panel-native landscape
if "--landscape" not in sys.argv:
    img = img.rotate(-90, expand=True)
args = [a for a in sys.argv[1:] if not a.startswith("--")]
img.save(args[0] if args else "screen.png")
print("saved", len(data))

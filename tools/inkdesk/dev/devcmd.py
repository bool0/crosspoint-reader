#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
"""Send a dev command and print its reply lines: devcmd.py "OPEN clock" [seconds]."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from x4link import X4  # noqa: E402

out = X4().command(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 3)
print("\n".join(line for line in out.splitlines() if any(k in line for k in ("_OK", "ERR", "SDFS", "I18N", "BATTERY"))))

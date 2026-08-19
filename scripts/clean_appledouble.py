#!/usr/bin/env python3
"""Remove macOS AppleDouble files (._*) that break compilation on exFAT volumes."""

from pathlib import Path

SKIP_DIRS = {".git", ".pio", ".cache", ".venv"}

for path in Path(".").rglob("._*"):
    if SKIP_DIRS.intersection(path.parts):
        continue
    try:
        path.unlink()
    except OSError:
        pass

#!/usr/bin/env python3
"""Compatibility entry point; the standalone engine owns its Ekho patch."""
import runpy
from pathlib import Path

runpy.run_path(str(Path(__file__).resolve().parent.parent / "engine/scripts/patch_ekho89.py"), run_name="__main__")

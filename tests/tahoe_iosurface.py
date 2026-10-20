#!/usr/bin/env python3
"""Exercise the Tahoe matrix bridge separately from the Sonoma/Sequoia path."""
from pathlib import Path
import subprocess,sys
subprocess.run([sys.executable,str(Path(__file__).with_name('sonoma_iosurface.py')),'--tahoe',*sys.argv[1:]],check=True)

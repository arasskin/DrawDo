#!/usr/bin/env python3
"""Build a self-contained local macOS demo with the native Datafriend library."""
import argparse
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROJECT = ROOT / 'src/todo_demo'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--flutter', default='flutter')
args = parser.parse_args()
subprocess.run(['python3', str(ROOT/'datafriend/tool/generate_attributes.py'), '--check'], check=True)
native = ROOT / 'datafriend/build/todo-demo-native'
subprocess.run(['cmake', '-S', str(ROOT / 'datafriend'), '-B', str(native),
                '-DCMAKE_BUILD_TYPE=Release', '-DDATAFRIEND_TESTS=OFF'], check=True)
subprocess.run(['cmake', '--build', str(native), '-j4'], check=True)
subprocess.run([args.flutter, 'build', 'macos', '--release'], cwd=PROJECT, check=True)
app = PROJECT / 'build/macos/Build/Products/Release/datafriend_todo_lab.app'
library = app / 'Contents/Frameworks/libdatafriend.dylib'
shutil.copy2(native / 'libdatafriend.dylib', library)
subprocess.run(['codesign', '--force', '--sign', '-', str(library)], check=True)
subprocess.run(['codesign', '--force', '--deep', '--sign', '-', '--entitlements',
                str(PROJECT / 'macos/Runner/Release.entitlements'), str(app)], check=True)
print(app)

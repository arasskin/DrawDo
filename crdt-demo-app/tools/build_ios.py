#!/usr/bin/env python3
"""CMake/Xcode device build, signing and deployment through Apple's command-line tools."""
import argparse, os, subprocess
from pathlib import Path
app=Path(__file__).resolve().parents[1];build=app/'build/ios'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('action',choices=['build','run'],nargs='?',default='build')
p.add_argument('--team',default=os.environ.get('DEVELOPMENT_TEAM'))
p.add_argument('--device',default=os.environ.get('IOS_DEVICE','Phone'))
p.add_argument('--timing',action='store_true')
p.add_argument('--frame-poll',action='store_true',help='Benchmark the previous frame-only processing loop')
a=p.parse_args()
if not a.team:
 cache=build/'CMakeCache.txt'
 if cache.exists():
  for line in cache.read_text().splitlines():
   if line.startswith('DEVELOPMENT_TEAM:STRING='):a.team=line.split('=',1)[1]
if not a.team:p.error('Set DEVELOPMENT_TEAM or pass --team for the first build')
def run(args):subprocess.run(list(map(str,args)),check=True)
run(['cmake','-S',app,'-B',build,'-G','Xcode','-DCMAKE_SYSTEM_NAME=iOS','-DCMAKE_OSX_SYSROOT=iphoneos',
     '-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0',
     '-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG',
     '-DCRDT_DEMO_EVENTS='+('OFF' if a.frame_poll else 'ON'),
     '-DCRDT_DEMO_TIMING='+('ON' if a.timing else 'OFF'),'-DCMAKE_OBJC_FLAGS_RELEASE=-O3 -DNDEBUG','-DDEVELOPMENT_TEAM='+a.team])
run(['cmake','--build',build,'--config','Release','--parallel','4','--','-allowProvisioningUpdates','-allowProvisioningDeviceRegistration'])
if a.action=='run':
 run(['xcrun','devicectl','device','install','app','--device',a.device,build/'Release-iphoneos/crdt_demo.app'])
 run(['xcrun','devicectl','device','process','launch','--device',a.device,'--terminate-existing','dev.drawdo.crdt'])

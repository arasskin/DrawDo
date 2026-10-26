#!/usr/bin/env python3
"""Direct Android build: CMake/Ninja, javac/d8, aapt2, zipalign/apksigner, adb."""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import time
import zipfile

app=Path(__file__).resolve().parents[1]
root=app.parent
build=app/'build/android'
sdk=Path(os.environ.get('ANDROID_HOME',str(Path.home()/'Library/Android/sdk')))
ndk=Path(os.environ.get('ANDROID_NDK_HOME',str(sdk/'ndk/28.2.13676358')))
java_home=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
env=dict(os.environ,JAVA_HOME=str(java_home))
tools=sdk/'build-tools/35.0.0'
android_jar=sdk/'platforms/android-35/android.jar'
llvm=ndk/'toolchains/llvm/prebuilt/darwin-x86_64'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('action',choices=['build','run','logs'],nargs='?',default='build')
parser.add_argument('--serial',default=os.environ.get('ANDROID_SERIAL'))
parser.add_argument('--api',type=int,choices=range(24,36),default=29,help='Minimum Android API level (default 29 enables Sokol Choreographer)')
a=parser.parse_args()
adb=[str(sdk/'platform-tools/adb')]+(['-s',a.serial] if a.serial else [])
pkg='dev.drawdo.app'
phases={}
def run(args,**kwargs):
    return subprocess.run(list(map(str,args)),check=True,env=env,**kwargs)
def timed(name,fn):
    start=time.monotonic();result=fn();phases[name]=time.monotonic()-start;return result
def fingerprint(paths,extra=''):
    h=hashlib.sha256(extra.encode())
    for p in paths:h.update(str(p).encode());h.update(p.read_bytes())
    return h.hexdigest()
def stale(stamp,value):return not stamp.exists() or stamp.read_text()!=value
def shell(*args):return run(adb+['shell',shlex.join(map(str,args))])
if a.action=='logs':
    run(adb+['logcat','-v','brief','DrawDo:I','SOKOL:W','AndroidRuntime:E','*:S']);raise SystemExit
build.mkdir(parents=True,exist_ok=True)
for path in [ndk/'build/cmake/android.toolchain.cmake',tools/'aapt2',android_jar,java_home/'bin/javac']:
    if not path.exists():raise SystemExit(f'Missing required tool: {path}')

def native():
    cache=build/'native/CMakeCache.txt'
    config_stamp=build/'native-config.stamp'
    config=str(ndk)+'|arm64-v8a|android-'+str(a.api)+'|RelWithDebInfo|-O3|c99'
    if not cache.exists() or stale(config_stamp,config):
        run(['cmake','-S',app,'-B',build/'native','-G','Ninja',
             '-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),
             '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-'+str(a.api),'-DANDROID_STL=none',
             '-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O3 -g -DNDEBUG',
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        config_stamp.write_text(config)
    run(['cmake','--build',build/'native','--parallel','4'])
timed('native',native)

def java():
    files=sorted((app/'android/java').rglob('*.java'))+sorted((root/'datafriend/platform/android/src/main/java').rglob('*.java'))
    for module in ['text_buffers', 'accessible_ui']:
        files += sorted((root/module/'android/java').rglob('*.java'))
    digest=fingerprint(files,str(java_home)+str(android_jar))
    stamp=build/'java.stamp'
    if not stale(stamp,digest):return
    classes=build/'classes';dex=build/'dex'
    if classes.exists():shutil.rmtree(classes)
    classes.mkdir();dex.mkdir(exist_ok=True)
    run([java_home/'bin/javac','--release','8','-classpath',android_jar,'-d',classes,*files])
    run([tools/'d8','--lib',android_jar,'--min-api','24','--output',dex,*sorted(classes.rglob('*.class'))])
    stamp.write_text(digest)
timed('java_dex',java)

def resources():
    files=sorted((app/'android/res').rglob('*.xml'))
    manifest=app/'android/AndroidManifest.xml'
    digest=fingerprint([manifest,*files],str(android_jar)+str(a.api))
    stamp=build/'resources.stamp'
    if not stale(stamp,digest):return
    res=build/'res';res.mkdir(exist_ok=True)
    for p in files:run([tools/'aapt2','compile',p,'-o',res])
    run([tools/'aapt2','link','-o',build/'resources.apk','--manifest',manifest,
         '-I',android_jar,'--min-sdk-version',str(a.api),'--target-sdk-version','35',*sorted(res.glob('*.flat'))])
    stamp.write_text(digest)
timed('resources',resources)

apk=build/'DrawDo.apk'
def package():
    libs=[build/'native/libdrawdo.so',build/'native/datafriend/libdatafriend.so']
    inputs=[build/'resources.apk',build/'dex/classes.dex',*libs]
    digest=fingerprint(inputs)
    stamp=build/'package.stamp'
    if not stale(stamp,digest) and apk.exists():return
    unsigned=build/'unsigned.apk';shutil.copyfile(build/'resources.apk',unsigned)
    stripped=build/'stripped';stripped.mkdir(exist_ok=True)
    with zipfile.ZipFile(unsigned,'a') as z:
        z.write(build/'dex/classes.dex','classes.dex',compress_type=zipfile.ZIP_DEFLATED)
        for p in libs:
            target=stripped/p.name;shutil.copyfile(p,target)
            run([llvm/'bin/llvm-strip','--strip-debug',target])
            z.write(target,'lib/arm64-v8a/'+p.name,compress_type=zipfile.ZIP_STORED)
    # Native libraries are stored uncompressed and aligned for direct loading.
    run([tools/'zipalign','-P','16','-f','4',unsigned,build/'aligned.apk'])
    key=app/'.android-debug.keystore'
    old_key=build/'debug.keystore'
    if not key.exists() and old_key.exists():shutil.copyfile(old_key,key)
    if not key.exists():
        run([java_home/'bin/keytool','-genkeypair','-keystore',key,'-storepass','android',
             '-alias','androiddebugkey','-keypass','android','-keyalg','RSA','-keysize','2048',
             '-validity','10000','-dname','CN=Android Debug,O=Android,C=US'])
    run([tools/'apksigner','sign','--ks',key,'--ks-pass','pass:android','--key-pass','pass:android',
         '--out',apk,build/'aligned.apk'])
    run([tools/'apksigner','verify',apk])
    run([tools/'zipalign','-c','-P','16','4',apk])
    stamp.write_text(digest)
timed('package_sign_verify',package)
if a.action=='run':
    timed('install',lambda:run(adb+['install','--no-incremental','-r',str(apk)]))
    shell('input','keyevent','KEYCODE_WAKEUP');shell('wm','dismiss-keyguard')
    shell('am','force-stop',pkg)
    timed('launch',lambda:shell('am','start','-W','-n',pkg+'/.MainActivity'))
print('\n'+str(apk)+f' ({apk.stat().st_size/1024/1024:.1f} MiB)')
print(', '.join(f'{k}: {v:.2f}s' for k,v in phases.items()))

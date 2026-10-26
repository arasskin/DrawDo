#!/usr/bin/env python3
"""Build a small dependency-free instrumentation APK and exercise the installed app."""
from pathlib import Path
import os, subprocess, zipfile, argparse
root=Path(__file__).resolve().parents[3]
sdk=Path(os.environ.get('ANDROID_HOME',str(Path.home()/'Library/Android/sdk')))
ndk=sdk/'ndk/28.2.13676358'; tools=sdk/'build-tools/35.0.0'
java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
parser=argparse.ArgumentParser();parser.add_argument('--serial',default='ZY227364TP');parser.add_argument('--mode',choices=['full','verify','cleanup'],default='full');args=parser.parse_args()
build=root/'DrawDo/build/device-tests';build.mkdir(parents=True,exist_ok=True)
source=Path(__file__).resolve().parent
classes=build/'classes';classes.mkdir(exist_ok=True)
dex=build/'dex';dex.mkdir(exist_ok=True)
env=dict(os.environ,JAVA_HOME=str(java))
def run(cmd):subprocess.run(list(map(str,cmd)),check=True,env=env)
jar=sdk/'platforms/android-35/android.jar'
run([java/'bin/javac','--release','8','-cp',jar,'-d',classes,source/'TodoVerification.java'])
run([tools/'d8','--lib',jar,'--min-api','24','--output',dex,*classes.rglob('*.class')])
manifest=build/'AndroidManifest.xml'
manifest.write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="dev.drawdo.app.tests">
<uses-sdk android:minSdkVersion="24" android:targetSdkVersion="35"/>
<application android:debuggable="true" android:extractNativeLibs="true"/>
<instrumentation android:name="dev.drawdo.app.tests.TodoVerification" android:targetPackage="dev.drawdo.app" android:functionalTest="true"/>
</manifest>''')
run([tools/'aapt2','link','-o',build/'tests-unsigned.apk','--manifest',manifest,'-I',jar])
with zipfile.ZipFile(build/'tests-unsigned.apk','a') as z:
 z.write(dex/'classes.dex','classes.dex')
run([tools/'zipalign','-f','4',build/'tests-unsigned.apk',build/'tests-aligned.apk'])
run([tools/'apksigner','sign','--ks',root/'DrawDo/.android-debug.keystore','--ks-pass','pass:android','--out',build/'tests.apk',build/'tests-aligned.apk'])
adb=[sdk/'platform-tools/adb','-s',args.serial]
run([*adb,'install','--no-incremental','-r',build/'tests.apk'])
run([*adb,'shell','am','force-stop','dev.drawdo.app'])
result=subprocess.run(list(map(str,[*adb,'shell','am','instrument','-w','-e','mode',args.mode,'dev.drawdo.app.tests/dev.drawdo.app.tests.TodoVerification'])),env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True)
print(result.stdout);(build/('result-'+args.mode+'.txt')).write_text(result.stdout)
if 'PASS:' not in result.stdout:raise SystemExit('Device tests failed')

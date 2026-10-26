"""Collect local native-producer -> text presentation benchmarks on both phones."""
import argparse, json, pathlib, subprocess as s, time, shlex
p = argparse.ArgumentParser()
p.add_argument('--kind', choices=['native', 'flutter'], required=True)
p.add_argument('--driver', type=pathlib.Path, required=True)
p.add_argument('--output', type=pathlib.Path, required=True)
a = p.parse_args()
out = a.output; out.mkdir(parents=True, exist_ok=False)
adb = ['/Users/alex/Library/Android/sdk/platform-tools/adb', '-s', 'ZY227364TP']
flutter = a.kind == 'flutter'
android = 'dev.drawdo.text_latency' if flutter else 'dev.drawdo.crdt'
ios = 'dev.drawdo.textLatency' if flutter else 'dev.drawdo.crdt'
def run(cmd, **kw): return s.run(cmd, check=True, **kw)
if not flutter:
    role = out / 'role.txt'; role.write_text('3\n')
    run(['xcrun', 'devicectl', '--timeout', '30', 'device', 'copy', 'to', '--device', 'Phone', '--domain-type', 'appDataContainer', '--domain-identifier', ios, '--source', str(role), '--destination', 'Library/Application Support/timing/run.txt'])
    run(adb + ['shell', 'run-as', android, 'sh', '-c', "'cat > files/timing/run.txt'"], input=b'3\n')
log = out / 'ios-driver.log'
f = log.open('w')
method = 'testFlutter' if flutter else 'testNative'
proc = s.Popen(['xcodebuild', 'test-without-building', '-project', str(a.driver / 'SchedulingTest.xcodeproj'), '-scheme', 'SchedulingTest', '-destination', 'id=00008110-000A7D681490401E', '-derivedDataPath', str(a.driver / 'build'), '-only-testing:SchedulingTest/TimingTest/' + method, 'DEVELOPMENT_TEAM=466U33S75F'], stdout=f, stderr=s.STDOUT)
deadline = time.monotonic() + 120
while 'SCHEDULING_STARTED' not in log.read_text():
    if proc.poll() is not None or time.monotonic() > deadline: raise RuntimeError('iOS driver did not start: ' + str(log))
    time.sleep(.5)
run(adb + ['shell', 'am', 'force-stop', android])
run(adb + ['shell', 'am', 'start', '-W', '-n', android + '/.MainActivity'])
time.sleep(2)
begins = time.monotonic(); frames = set(); layers = []
while time.monotonic() - begins < 60:
    names = run(adb + ['shell', 'dumpsys', 'SurfaceFlinger', '--list'], capture_output=True, text=True).stdout.splitlines()
    candidates = [n for n in names if (android in n and 'SurfaceView' in n) if 'Background for' not in n] if flutter else [n for n in names if n.startswith('PopupWindow:')]
    if len(candidates) != 1: raise RuntimeError('Unexpected layers: ' + str(candidates) + '\n' + '\n'.join(names))
    layer = candidates[0]
    if layer not in layers: layers.append(layer)
    data = run(adb + ['shell', 'dumpsys', 'SurfaceFlinger', '--latency', shlex.quote(layer)], capture_output=True, text=True).stdout
    for line in data.splitlines()[1:]:
        cols = line.split()
        if len(cols) == 3:
            row = tuple(map(int, cols))
            if all(0 < v < 2**63 - 1 for v in row): frames.add(row)
    print(f'{time.monotonic()-begins:.1f}s: {len(frames)} frames', flush=True)
    time.sleep(2)
(out / 'android-surface.csv').write_text('desired_ns,present_ns,ready_ns\n' + ''.join(','.join(map(str, r))+'\n' for r in sorted(frames)))
(out / 'layers.json').write_text(json.dumps(layers))
with (out / 'android-events.csv').open('wb') as dst:
    cmd = ['exec-out', 'cat', f'/sdcard/Android/data/{android}/files/events.csv'] if flutter else ['exec-out', 'run-as', android, 'cat', 'files/timing/events.csv']
    run(adb + cmd, stdout=dst)
source = 'Documents/timing' if flutter else 'Library/Application Support/timing'
run(['xcrun', 'devicectl', '--timeout', '30', 'device', 'copy', 'from', '--device', 'Phone', '--domain-type', 'appDataContainer', '--domain-identifier', ios, '--source', source, '--destination', str(out / 'ios')])
code = proc.wait(timeout=90)
if code: raise RuntimeError('iOS driver failed: ' + str(code))
print('Saved', out, flush=True)

"""Bound sanitizer runs: some Apple ASan runtimes deadlock before main."""
import os
import subprocess
import sys

for executable in sys.argv[1:]:
    print(f'Running {executable}', flush=True)
    try:
        subprocess.run([executable], check=True, timeout=60,
                       env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'))
    except subprocess.TimeoutExpired:
        sys.exit(f'{executable}: exceeded 60 seconds; investigate runtime startup or test hang')
    except subprocess.CalledProcessError as error:
        sys.exit(f'{executable}: failed with exit code {error.returncode}')

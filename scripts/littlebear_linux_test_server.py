#!/usr/bin/env python3
"""Test-only launcher: run the Linux/io_uring authenticated TCP server on a loopback-published port."""
import os
from pathlib import Path
import socket
import sys
import uuid

repo = Path(__file__).resolve().parents[1]
arguments = sys.argv[1:]
values = Path(arguments[arguments.index('--values') + 1])
port_index = arguments.index('--port') + 1
if arguments[port_index] == '0':
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        arguments[port_index] = str(listener.getsockname()[1])
port = arguments[port_index]
# Only ephemeral test data and the test binary are mounted. Host port is loopback-only.
command = ['docker', 'run', '--rm', '--name', 'datafriend-tcp-' + uuid.uuid4().hex,
           '--cpus=1', '--memory=256m', '--security-opt', 'seccomp=unconfined',
           '-p', f'127.0.0.1:{port}:{port}',
           '-v', f'{values.parent.resolve()}:{values.parent}',
           '-v', os.environ.get('DATAFRIEND_LINUX_SERVER', str(repo/'littlebear/build/server')) + ':/server:ro',
           '-e', 'ASAN_OPTIONS=detect_leaks=1:quarantine_size_mb=16',
           '-e', 'UBSAN_OPTIONS=halt_on_error=1',
           os.environ.get('DATAFRIEND_LINUX_IMAGE', 'drawdo-auth-validation'),
           '/server', '--bind', '0.0.0.0', *arguments]
os.execvp(command[0], command)

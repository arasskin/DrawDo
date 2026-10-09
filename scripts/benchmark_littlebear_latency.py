#!/usr/bin/env python3
"""Time read-only LBS5 heartbeats on one persistent TCP connection (no UI loop)."""
import argparse
import json
from pathlib import Path
import re
import socket
import statistics
import struct
import time


def blob(value):
    return struct.pack('!I', len(value)) + value


def exact(connection, size):
    data = bytearray()
    while len(data) < size:
        part = connection.recv(size - len(data))
        if not part:
            raise RuntimeError('Incomplete heartbeat response')
        data.extend(part)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--port', type=int, default=7777)
    parser.add_argument('--count', type=int, default=100)
    parser.add_argument('--schema-file', type=Path)
    args = parser.parse_args()
    if args.count < 1:
        parser.error('--count must be positive')
    if args.schema_file:
        schema = args.schema_file.read_text().strip()
    else:
        header = Path(__file__).resolve().parents[1] / 'datafriend/include/datafriend/attributes.h'
        schema = re.search(r'attribute_schema = "([0-9a-f]+)"', header.read_text()).group(1)
    samples = []
    with socket.create_connection((args.host, args.port), timeout=5) as connection:
        connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        for request in range(1, args.count + 1):
            packet = (b'LBS\5' + struct.pack('!IIB', request, 0, 7)
                      + blob(b'') + blob(schema.encode())
                      + struct.pack('!QQ', 0, 0) + blob(b''))
            start = time.perf_counter_ns()
            connection.sendall(blob(packet))
            length, = struct.unpack('!I', exact(connection, 4))
            if length != 25:
                raise RuntimeError('Unexpected heartbeat length')
            reply = exact(connection, length)
            samples.append((time.perf_counter_ns() - start) / 1e6)
            if reply != struct.pack('!IIBQQ', request, 0, 0, 0, 0):
                raise RuntimeError('Unexpected heartbeat response')
    print(json.dumps({'host': args.host, 'port': args.port, 'count': len(samples),
                     'connection_setup_included': False,
                     'milliseconds': {'min': min(samples), 'p50': statistics.median(samples),
                                      'p95': sorted(samples)[min(len(samples)-1, int(len(samples)*.95))],
                                      'max': max(samples)}, 'samples_ms': samples}, indent=2))


if __name__ == '__main__':
    main()

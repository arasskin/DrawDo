#!/usr/bin/env python3
"""Fetch the pinned upstream B-tree for optional historical benchmarks only."""
from pathlib import Path
import hashlib
import urllib.request

REVISION = "b3687652922785c6f3b6c00684f51c6977965784"
FILES = {
    'cpp/negentropy/encoding.h': '9c1f12b7831fb5aed466dbaa2560d36b91d7afdd988ac126409050c313a0cf3a',
    'cpp/negentropy/storage/BTreeLMDB.h': '635c470e66624bfc9111b383321808dce25a6115c3b30f45e0a66296288e68a0',
    'cpp/negentropy/storage/BTreeMem.h': '650e88f8a49ad13de764fe2c12dcc1cecec052f9a9a76cf83ad178b6462a949a',
    'cpp/negentropy/storage/SubRange.h': '2de7242bb0401d37afda48bfce40684dfbe068a2f00bda359180adcee2a7f95f',
    'cpp/negentropy/storage/Vector.h': '705b5c8aeb5fb82b9a419aee7962a1e21160fec7c713a23ff32571b638339059',
    'cpp/negentropy/storage/base.h': '6a57bde748287bb912fb130562351b6fd5f6a32d7cf13ead9a56fa9bbc9f3817',
    'cpp/negentropy/storage/btree/core.h': 'c2a17e978fa8aeb5cd801719439516b3cb5ecfe6b40d90ae9c49f3ebafb2d5cd',
    'cpp/negentropy/storage/btree/debug.h': '2ddd8b8e70e257b27ff85fa48ef9d5e44b54d0765611d28c17e844ec01be6d8c',
    'cpp/negentropy/types.h': '6d15fd876a173c6fa7d3f0dd9bb67738f60250e9cd3ac043b40053a21b277874',
    'cpp/negentropy.h': '8515f87b6c662d70fe55ada8f10b3883293020747d487d830948d64cb7c0b1a2',
    'LICENSE': 'd80d117cca797e5a0d7cf6ead0f7b4cfaf05c6c2ea7d60eeaf49e40bfdafeb27',
}
ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/reference/negentropy"
for name, digest in FILES.items():
    path = OUTPUT / name
    if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == digest:
        continue
    url = f"https://raw.githubusercontent.com/hoytech/negentropy/{REVISION}/{name}"
    data = urllib.request.urlopen(url, timeout=30).read()
    if hashlib.sha256(data).hexdigest() != digest:
        raise RuntimeError(f"unexpected upstream bytes: {name}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
(OUTPUT / ".ready").write_text(REVISION + "\n")
print(f"Verified optional upstream benchmark reference at {OUTPUT}")

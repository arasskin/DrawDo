"""Reuse upstream BTreeCore verbatim except its namespace and type include."""
from pathlib import Path
import hashlib

root = Path(__file__).resolve().parents[1]
source = root / 'build/reference/negentropy/cpp/negentropy/storage/btree/core.h'
text = source.read_text()
assert text.count('#include "negentropy.h"') == 1
text = text.replace('#include "negentropy.h"', '#include "btree_xor_types.h"')
text = text.replace('namespace negentropy {', 'namespace negentropy_xor {')
text = text.replace('negentropy::', 'negentropy_xor::')
output = root / 'build/generated/btree_xor_core.h'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text('// Generated from upstream core.h, SHA256 ' + hashlib.sha256(source.read_bytes()).hexdigest() + '\n' + text)

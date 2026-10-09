# Source provenance

The mutable and persistent wide trees adapt the hash-ranked wide-tree algorithm and leaf-only
value layout from ClojureDart's `HashRankedWideTreapMap`, in
`clj/src/cljd/core.cljd` at commit
`4ee792f3044589c96b019c2a86db9b23f1b283ab`.

Original copyright (c) Baptiste Dupuch & Christophe Grand. All rights reserved.
The original source is covered by the Eclipse Public License 1.0; a copy is
included in [licenses/epl-v10.html](licenses/epl-v10.html). Its notices must not
be removed. The C++ adaptation adds mutable allocation, per-entry fingerprints,
subtree XOR summaries, rank/select, and bounded range enumeration.

The benchmark uses the existing LittleBear headers without modification,
including their bundled xxHash header and its BSD-2-Clause notice. The wire
harness is a custom range-reconciliation experiment, not upstream Negentropy
wire format or a port of its implementation.

The optional B-tree benchmarks fetch Negentropy's `BTreeCore` at revision
`b3687652922785c6f3b6c00684f51c6977965784` into the ignored
`build/reference/negentropy/` directory. Copyright (c) 2023 Doug Hoyte; its MIT
license is fetched and retained at `build/reference/negentropy/LICENSE`.
`tool/fetch_reference.py` verifies source hashes before use.
`benchmarks/generate_btree.py` generates a second copy under the build directory,
changing its namespace and type include for the 64-bit XOR comparison. This is
an optional historical measurement baseline, not checked-in production code.
The benchmark node store follows `BTreeMem`'s node-ID/hash-table storage with
separate roots sharing a pool for the many-namespace workload.

The persistent adaptation adds immutable path copying, separate triple payloads,
atomic reference counting and allocation-failure rollback under the same EPL-1.0
notice. Datafriend's native engine uses the bundled xxHash header (BSD-2-Clause),
`third_party/unordered_dense` (MIT), and the pinned `third_party/aelmdb` sources
and license. AELMDB's XOR aggregate and distinct format-tag changes are documented
with upstream hashes in that directory. Local view reconciliation uses the
independent shared `littlebear-benchmarks/src/reconcile.h` implementation (LBR3).

Native owner identities use Monocypher 4.0.3 core and optional Ed25519 sources,
vendor-copied unchanged under `third_party/monocypher`, with their dual BSD-2-Clause /
CC0 license and upstream SHA-512 archive checksum in `UPSTREAM.json`. The SHA-512
Ed25519 API is used for RFC 8032 compatibility with Littlebear's libsodium verifier.

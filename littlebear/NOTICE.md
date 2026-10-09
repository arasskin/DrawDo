# Wide-tree source provenance

LittleBear's mutable wide index uses the shared implementation at
`datafriend/include/datafriend/wide_tree.h`, adapted from ClojureDart's
`HashRankedWideTreapMap`.

Original copyright (c) Baptiste Dupuch & Christophe Grand. All rights reserved.
See [the shared source notice](../datafriend/NOTICE.md) and the included
[Eclipse Public License 1.0](../datafriend/licenses/epl-v10.html).

The backend facade, slab pool and allocation-failure integration preserve that
provenance. The repository's bundled xxHash retains its own BSD-2-Clause notice.

LittleBear's namespace directory uses `ankerl::unordered_dense` v5.1.0, pinned at
`b7cf409efee3a4419191bb3db29054b2ff6dc02d`. Copyright (c) 2022 Martin Leitner-Ankerl.
See the vendored [MIT license](../third_party/unordered_dense/LICENSE) and
[dependency provenance](../third_party/unordered_dense/README.md).

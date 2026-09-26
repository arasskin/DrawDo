# DrawDo

DrawDo is a fresh rebuild of iCare for iOS and Android: tasks, friends, drawing,
and playful physical interactions, backed by littlebear.

The client direction is **Kotlin Multiplatform with Compose Multiplatform**.
Littlebear remains C++, including its custom storage design. Reconciliation will
use the upstream negentropy implementation through a small native interface.

This first commit brings together the source material. There is no Kotlin app
scaffold or working end-to-end integration yet.

## Repository

| Path | Contents |
| --- | --- |
| `littlebear/` | Imported C++ backend prototype; the starting point for further development |
| `third_party/negentropy/` | Pinned upstream C++ protocol engine and C bindings |
| `assets/` | iCare illustrations, animations, and icons for the new client |
| `reference/icare/` | ClojureDart client and Clojure backend source snapshot for porting behavior |
| `docs/starting-point.md` | Current state, architectural decisions, and first development milestones |
| `docs/provenance.md` | Import sources and revisions |

## Development direction

- Share the application model, physics, sync coordination, and UI in Kotlin.
- Keep platform services and native-library bindings behind small interfaces.
- Continue developing littlebear's custom data structures deliberately, with
  correctness tests before integration.
- Preserve the original projects in `summer` as reference material.

Littlebear currently targets Linux with C++23 and `liburing`. Its existing
`make -C littlebear` and `make -C littlebear debug` commands build the server and
sanitizer configuration respectively on a suitable Linux environment. It is
still a prototype, not a functioning storage backend.

The original iCare source is reference material, not a buildable Flutter project
in this repository. Generated Dart, platform projects, local databases, and
credentials have not been imported.

# Bootstrap progress ledger

Updated: 2026-10-15

## Completed evidence

- The existing GitHub repository is attached as `origin`, and local `main` tracks `origin/main`.
- Installed runtime verified as `4.7.2.stable.cafecito_e76255129.ed1daf0bf`.
- Custom build repository pinned at `e762551293cd7312e0fd6695b76b1050d7e15bdc`.
- Embedded Godot source pinned at `ed1daf0bf001b61586d9930840f2f1394092c079`.
- Official template pinned at `d36f9d03e9e02dad22829f1faf4e03a347616df5`.
- godot-cpp pinned at `507ed9d840c01a3c5b2a39af8bb4000bfac30bf5`.
- The installed engine's exact extension API was dumped and hashed.
- The GDScript layers and direct engine-private include surface were inventoried.
- ADR 0001 selects a Mocha-owned GDExtension port and rejects delegation/private ABI.
- The official-template-derived extension builds against the exact custom API.
- Godot registers `MochaScript`, recognizes `.mocha`, and round-trips source through the
  Mocha resource loader without invoking built-in GDScript.
- `./scripts/verify.sh` is the canonical bootstrap verifier and emits `MOCHA_VERIFY_OK`.
- The imported tokenizer, Mocha parser/AST, analyzer, compiler, bytecode VM, and complete
  `GDExtensionScriptInstanceInfo3` path execute without built-in GDScript delegation.
- `example.mocha` attaches to a Godot `Node`, calls `get_name()`, mutates a typed member, and returns
  deterministic result `42` using arrays, a dictionary, indexing, calls, and control flow.
- Syntax and type fixtures assert stable codes plus source path, line, and column.
- Direct native C++ layer tests and a pinned three-fixture upstream frontend baseline run in the
  canonical verifier; the imported fixtures are parse/analyze evidence, not runtime parity.
- `mocha_language_version()` proves intentional Mocha-only evolution across all execution layers.
- Source materialization, create-only import, and review-only upstream delta scripts are verified.
- Linux CI pins the private custom-engine release asset by immutable ID and SHA-256.

## Current checkpoint

Harden and expand the executable bootstrap subset while preserving the owned language boundary.

## Required later checkpoints

- [x] deterministic upstream materialization and review-only update tooling
- [x] tokenizer and parser path plus pinned bootstrap corpus
- [x] analyzer path with path/line/column diagnostics
- [x] compiler, bytecode, and VM path
- [x] complete script-instance execution path
- [x] positive vertical-slice fixture
- [x] negative syntax and type fixtures
- [x] Mocha-only evolution canary
- [ ] canonical clean-checkout verification command (working-tree verifier is green; clone audit pending)
- [x] Linux CI against a pinned custom-engine artifact
- [x] contributor and architecture documentation for the bootstrap seams

## Known unsupported behavior

The supported subset is listed in `docs/language/specification.md`. In particular, script
inheritance, classes, signals, annotations/exports, `for`, `match`, async/coroutines, typed
collection enforcement, index/member assignment, global utility functions, and editor completion
remain unsupported. Recognized unsupported statements fail with `M1090`; top-level declaration
gaps fail with `M1008`, invalid assignment targets with `M2090`, and compiler gaps with `M3xxx`
rather than succeeding as no-ops.

## Next action

Verify a local clean clone, run an independent branch review, and fix every concrete finding before
the bootstrap handoff.

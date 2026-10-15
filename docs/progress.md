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
- GDScript compatibility is a bootstrap reference point, not a compatibility contract: imported
  code is Mocha-owned, upstream changes are review-only inputs, and deliberate semantic divergence
  is versioned and tested in MochaScript itself.
- `example.mocha` attaches to a Godot `Node`, calls `get_name()`, mutates a typed member, and returns
  deterministic result `42` using arrays, a dictionary, indexing, calls, and control flow.
- Syntax and type fixtures assert stable codes plus source path, line, and column.
- Direct native C++ layer tests and a pinned three-fixture upstream frontend baseline run in the
  canonical verifier; the imported fixtures are parse/analyze evidence, not runtime parity.
- `mocha_language_version()` proves intentional Mocha-only evolution across all execution layers.
- Runtime failures carry exact tested source spans; failures captured across an engine-mediated
  nested Mocha call are re-attributed to the call site without leaking inner Godot argument metadata.
- Omitted declarations infer carriers, explicit `Variant` remains dynamic, and unsafe current or
  forward reads of members whose carrier is not yet inferred fail statically with `M2032`.
- Source materialization, create-only import, and review-only upstream delta scripts are verified.
- Explicit upstream review is read-only: it disables optional Git locks and lazy fetches, verifies
  all required provenance objects locally, and keeps its coordination lock in MochaScript's cache.
- Debug and release extension builds are isolated and exercised in copied Godot projects; the
  release smoke test derives its library mapping from the manifest rather than assuming a filename.
- The managed, hash-pinned SCons environment is reproducible under externally managed Python and
  is insulated from user site tools, site directories, SCons flags, and SCons cache settings.
- Linux CI pins the private custom-engine release asset by immutable ID and SHA-256.
- A recursive clean clone builds from the tracked tree and passes `./scripts/verify.sh`.

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
- [x] canonical clean-checkout verification command
- [x] Linux CI against a pinned custom-engine artifact
- [x] contributor and architecture documentation for the bootstrap seams

## Known unsupported behavior

The supported subset is listed in `docs/language/specification.md`. In particular, script
inheritance, classes, signals, annotations/exports, `for`, `match`, async/coroutines, runtime typed
collection enforcement, index/member assignment, global utility functions, and editor completion
remain unsupported. Collection type parameters are retained and their nested type names are
analyzed, but runtime values are currently erased to `Array`/`Dictionary`. Recognized unsupported
statements fail with `M1090`; top-level declaration gaps fail with `M1008`, invalid assignment
targets with `M2090`, and compiler gaps with `M3xxx` rather than succeeding as no-ops.

## Next action

Use the divergence workflow to specify and implement the first post-bootstrap language feature.
The recommended first seam is lexical-scope metadata, followed by full control-flow return analysis.

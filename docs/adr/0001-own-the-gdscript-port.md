# ADR 0001: Own the GDScript port behind the GDExtension boundary

- Status: Accepted
- Date: 2026-10-15
- Baseline: `upstream.lock.json`

## Context

MochaScript starts from the GDScript implementation shipped by Cafecito's custom Godot, but it is
intended to become a separate language. The initial source is therefore provenance, not a permanent
compatibility contract.

The installed release has two source identities:

1. `cafecito-games/custom-godot` commit `e762551293cd7312e0fd6695b76b1050d7e15bdc`
   owns the build and enabled patchers.
2. Its `godot/` submodule pins Godot commit
   `ed1daf0bf001b61586d9930840f2f1394092c079` (`4.7.2-stable`).

The release's enabled `autoload-path-uid` patch modifies
`modules/gdscript/gdscript_analyzer.cpp` and GDScript editor/test code. Disabled GDScript patchers
are not part of the baseline. The installed binary reports
`4.7.2.stable.cafecito_e76255129.ed1daf0bf`; its dumped extension API has SHA-256
`b61fb6208257c8d694463404d93d0b4d39e4d11a772babf012150bd57411d41b`, and the executable itself
has SHA-256 `de80b383661a10ef263ef30aef9f9de89114f6f4d14e6e1b350779f7aaa912e3`.

The non-test GDScript implementation is not a standalone library. Direct inspection of the pinned
source found 146 includes from `core/`, 36 from `editor/`, seven from `scene/`, two from other
`modules/`, and three from `servers/`. The important top-level code sizes are:

| Layer | C++ lines in the pinned snapshot | Main private dependencies |
| --- | ---: | --- |
| tokenizer | 2,552 | engine strings, containers, error macros |
| parser and AST | 8,207 | tokenizer, Variant values, project/resource access |
| analyzer | 6,854 | parser cache, ClassDB, ScriptServer, resources |
| compiler and bytecode generator | 6,222 | analyzed AST, `GDScript`, method binds, Variant internals |
| VM and runtime objects | 6,424 | `ScriptInstance`, `CallableCustom`, debugger/profiler, Variant internals |
| cache | 647 | parser/analyzer/script resources, file and resource loaders |
| language/resource integration | 4,096 | `ScriptLanguage`, `Script`, loaders, debugger/editor services |
| editor/debug support | 6,440 | editor-private APIs |

Godot's internal `String`, `Variant`, `Object`, `Ref`, `Script`, and container types are not the
same C++ API as their godot-cpp wrappers, even where names and behavior resemble each other.
Compiling the module unchanged and resolving private symbols from the editor executable would make
the extension depend on an undocumented C++ ABI. It would also prevent distributing or evolving
the language independently.

The public extension surface does expose `ScriptLanguageExtension`, `ScriptExtension`, resource
loaders, and the raw `GDExtensionScriptInstanceInfo3` vtable. The working sibling BaristaScript
implementation is evidence that a custom bytecode runtime and complete script instance can be
hosted through that surface. It is prior art for integration only, not the source of MochaScript's
language semantics.

## Decision

MochaScript will be a Mocha-owned port of the pinned GDScript snapshot, compiled into a
GDExtension and renamed/namespaced so it cannot collide with built-in GDScript.

The codebase will keep these boundaries:

- `src/language/`: tokenizer, AST/parser, type analysis, and compiler.
- `src/runtime/`: Mocha bytecode representation and VM.
- `src/platform/`: explicit adapters for godot-cpp strings, containers, Variant operations,
  native metadata, files, settings, and diagnostics.
- `src/integration/`: `MochaScriptLanguage`, `MochaScript`, resource loading, script instances,
  debugger/editor-facing services, and extension registration.
- `project/tests/upstream/`: pinned bootstrap evidence inherited from GDScript.
- `project/tests/`: authoritative MochaScript integration behavior, including deliberate divergence.

The port may preserve recognizable upstream control flow and file boundaries when useful, but
Mocha-owned names, APIs, tests, and design take precedence after import. Future upstream changes
are reviewed as candidate changes; they are never merged or regenerated over Mocha sources
automatically.

The initial implementation will use only the public GDExtension ABI. A custom-engine bridge may be
introduced only after a focused failing experiment proves that a required behavior cannot be
implemented through that ABI. Any bridge must be versioned, minimal, separately documented, and
covered by a test. It must not expose arbitrary private C++ objects or turn MochaScript into a
front-end for built-in GDScript.

## Rejected alternatives

### Transpile `.mocha` to GDScript

Rejected because built-in GDScript would remain the parser/analyzer/compiler/runtime authority.
Language changes would be constrained by what can be encoded as GDScript, and diagnostics and
runtime semantics would not belong to MochaScript.

### Delegate through a custom engine API to the built-in GDScript compiler

Rejected for the same ownership reason. A bridge may expose missing engine services, but it may not
compile or execute MochaScript as GDScript.

### Compile `modules/gdscript` unchanged against engine-private headers

Rejected because that relies on private C++ ABI and symbol visibility. A release that happens to
load is not a supported extension contract.

### Keep MochaScript as a perpetual patch stack over GDScript

Rejected because easy rebasing would become a design constraint. The import/update tooling will
produce provenance and reviewable diffs, not overwrite Mocha-owned code.

## Staged implementation

1. Pin and verify both engine source identities, the patcher input, the exact extension API, the
   official template, and godot-cpp.
2. Establish the official-template build and `.mocha` language/resource recognition.
3. Port tokenizer and parser through the platform seam; import a parser corpus subset.
4. Port analyzer and native metadata/resource resolution; stabilize Mocha diagnostics.
5. Port compiler, bytecode, VM, and script-instance execution as a vertical slice.
6. Expand conformance, editor/debug integration, and explicit unsupported-feature failures.

Every stage must leave a runnable verification surface. Recognition alone is not the final goal.

## Consequences

The initial port is more work than delegation, but it creates the ownership boundary required for
language evolution. Some engine facilities will need Mocha-owned representations or slower public
GDExtension calls. Upstream updates become intentional engineering work. In exchange, syntax,
analysis, bytecode, runtime behavior, and diagnostics can diverge without negotiating with the
built-in language.

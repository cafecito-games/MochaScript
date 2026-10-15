# Architecture

MochaScript is a language implementation hosted by Godot through the public GDExtension ABI. It
does not invoke the built-in GDScript parser, compiler, VM, or script instances. The only code path
from a `.mocha` file to execution is:

```text
ResourceFormatLoader
  -> MochaScript
  -> tokenizer -> parser/AST -> analyzer -> compiler
  -> Mocha bytecode -> Mocha VM
  -> GDExtensionScriptInstanceInfo3 -> Godot Object
```

## Component boundaries

- `src/language/` owns tokens, AST, parsing, semantic types, diagnostics, and bytecode emission.
  The tokenizer is an owned port of the pinned Godot source; subsequent files are Mocha-owned
  adaptations around that representation.
- `src/runtime/` owns the bytecode model and VM. It may use public `Variant` operations and public
  Godot method dispatch, but never built-in GDScript execution.
- `src/platform/mocha_platform.h` is the single mapping from engine-module concepts to public
  godot-cpp types. Missing APIs are explicit adaptations, not empty shims.
- `src/integration/` owns resource loading, `ScriptExtension`, `ScriptLanguageExtension`, and the
  manually populated script-instance vtable. Its vtable pattern is adapted from BaristaScript.
- `src/testing/` contains native C++ pipeline assertions invoked in the custom engine process.

The AST is intentionally independent of `Object`, resources, editor state, and script instances.
It uses Godot value types (`String`, `StringName`, and `Variant`) through the platform seam because
those are the language's runtime carriers. Engine objects enter only at VM host calls and the
integration boundary.

## Execution and ownership

`MochaScript::_reload()` is transactional: it tokenizes, parses, analyzes, and compiles in order,
then publishes one complete module. No executable module is published after an error.
`can_instantiate()` becomes true only when a complete `CompiledModule` exists. Reload is refused
with `ERR_ALREADY_IN_USE` while an instance is alive; this prevents bytecode and member-layout
replacement beneath active frames. Each VM call also retains its module for the complete frame.
Source replacement is likewise refused with diagnostic `M0003` while an instance is alive, so the
published source and executable module cannot disagree. With no live instance, changing source
immediately invalidates and discards the old AST and module. A path-backed resource recompiles
transactionally at once (including `CACHE_MODE_REPLACE`); a new in-memory resource becomes
executable after its explicit reload. Replacing cached source while a runtime instance is alive
fails with `ERR_ALREADY_IN_USE` and preserves the old source and executable module.
`MochaScriptResourceSaver` persists source through Godot's `ResourceSaver`, so editor-created and
editor-modified `.mocha` resources use the same load/compile path after saving.

Each attached object receives a `MochaScriptInstance` containing its own member vector and a strong
reference to the script. Godot reaches it through `GDExtensionScriptInstanceInfo3`; startup refuses
registration if any callback slot is absent. Calls enter `MochaVM`, which interprets Mocha bytecode
and uses `Variant::callp` only for ordinary public Godot object methods. The VM has instruction and
recursion limits shared across nested and engine-reentrant calls, and reports source-located
`M4xxx` failures.

The bootstrap has no tool scripts. In editor context a valid script therefore reports that it cannot
instantiate, and Godot uses the placeholder-instance path; member initializers and user callbacks do
not execute in an open editor scene.

## Diagnostics

Diagnostics are stable Mocha-owned records with `code`, `message`, `path`, `line`, `column`,
`end_line`, and `end_column`:

- `M0xxx`: tokenizer and resource/integration lifecycle;
- `M1xxx`: parser;
- `M2xxx`: analyzer;
- `M3xxx`: compiler or recognized-but-not-executable construct;
- `M4xxx`: runtime.

Unsupported constructs must stop in one of those layers. They may not compile to a no-op or fall
back to GDScript.

## Engine dependency policy

The current implementation requires no custom private engine bridge. It targets the exact public
extension API checked into `api/extension_api.json`. A future missing capability must first have a
focused failing test. If a bridge is unavoidable, it must be versioned, minimal, represented as a
reproducible custom-godot patch, and isolated behind `src/platform/`; private C++ objects or ABI
linkage are not acceptable.

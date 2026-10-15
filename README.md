# MochaScript

MochaScript is an executable C++ GDExtension scripting language for Godot. It begins with the
GDScript implementation from Cafecito's pinned custom Godot release as bootstrap provenance, then
becomes an independently specified and maintained language. Compatibility with future GDScript is
not a project constraint.

The bootstrap vertical slice is executable. A `.mocha` resource is tokenized, parsed, statically
analyzed, compiled to Mocha bytecode, attached to a Godot object through a native script instance,
and executed by the Mocha VM. The implemented grammar is intentionally a coherent subset; see the
[bootstrap specification](docs/language/specification.md) for the exact working and unsupported
surface.

## Bootstrap verification

Prerequisites are Git, Python 3, SCons, and the pinned Cafecito Godot binary on `PATH`.

```sh
./scripts/setup.sh
./scripts/verify.sh
```

`verify.sh` derives the real CPU quota from cgroup v2, caps the memory-heavy binding build at four
jobs, verifies the build-tool version, godot-cpp checkout, extension API, and installed runtime
binary pins, builds the extension, imports the fixture project headlessly, and requires every
layer-specific sentinel plus the final `MOCHA_VERIFY_OK` result. Both debug and release extension
libraries must build, the release library and entry symbol are load-probed without shipping the
native test harness, and isolated editor/recognition/execution smoke tests run against the release
binary. Every script resource under `project/` must have a committed UID sidecar that editor import
neither rewrites nor leaves untracked. `materialize_upstream.sh` verifies
the custom-Godot/Godot source and import pins; Linux CI additionally verifies the downloaded SCons
wheel and editor archive hashes.

The fixture covers typed members, locals and functions, arrays, dictionaries, indexing, calls,
conditionals, state mutation, and interaction with a Godot `Node`. Negative fixtures assert stable
syntax and type diagnostics with path, line, and column.

## Language ownership

Imported code is a starting point, not a patch queue. [ADR 0001](docs/adr/0001-own-the-gdscript-port.md)
defines the ownership boundary, while [the divergence ledger](docs/language/divergence.md) records
inherited, intentional, planned, and unsupported behavior. Once MochaScript changes a rule, its own
specification and conformance tests are authoritative.

## Development guides

- [Architecture](docs/architecture.md)
- [Source provenance and upstream review](docs/provenance.md)
- [Language evolution guide](docs/contributing-language.md)
- [Testing and pinned CI](docs/testing.md)

# Testing and CI

Run the complete local suite with:

```sh
./scripts/verify.sh
```

The build requires a C++17 compiler and exactly the SCons version pinned in
`upstream.lock.json`. `./scripts/install_scons.sh` reproducibly installs its hash-verified wheel into
a wheel-hash-and-Python-ABI-keyed virtualenv under the user's persistent data directory. `setup.sh`
and `build.sh` resolve it directly without relying on `PATH`; `setup.sh` intentionally verifies rather than
mutates the checkout and reports the explicit submodule initialization command when needed.

It validates immutable pins, uses the cgroup CPU quota with a four-job ceiling, builds debug and
release extension libraries with target-specific object caches, and load-probes the release library
and its entry symbol. It then imports the fixture project and runs these isolated sentinels exactly
once against the debug library. A copied project remaps its debug manifest slot to the release
library declared by the shipping `.release` key and repeats editor, recognition, and VM-execution
smoke tests against the optimized binary. Both manifest paths must resolve to their corresponding
built artifacts. That lane also proves the debug-only native C++ test class is not registered in
release builds. Both lanes use fresh copies that exclude persistent `.godot` state.

- `MOCHA_RECOGNITION_OK`: language/resource identity and source preservation;
- `MOCHA_EDITOR_OK`: non-tool scripts use placeholders and cannot instantiate in editor context,
  committed script UIDs resolve through the editor registry, and new `.mocha` resources save and reload;
- `MOCHA_TOKENIZER_OK`: positive tokens and a stable lexical error;
- `MOCHA_FRONTEND_OK`: parser and analyzer positive/negative cases;
- `MOCHA_EXECUTION_OK`: compile, instantiate, host-object interaction, VM execution, and divergence
  canary;
- `MOCHA_NATIVE_OK`: direct C++ tokenizer/parser/analyzer/compiler/VM tests;
- `MOCHA_BOOTSTRAP_CORPUS_OK`: pinned upstream frontend acceptance/rejection evidence (not runtime
  parity);
- `MOCHA_VERIFY_OK`: the aggregate result.

Any timeout, crash, unexpected engine error, missing or duplicate sentinel, unexpected diagnostic,
or nonzero exit fails verification. The pinned engine's headless editor-script teardown emits five
known renderer/text RID allocation errors; only those exact RID types are allowlisted for the editor
placeholder test. Each verifier invocation gets isolated XDG data, config, and cache roots inside
its log directory, so fixed `user://` fixtures cannot collide across worktrees. Logs are written
under the user's cache rather than the repository.

Linux CI reads an immutable same-repository mirror asset ID, archive SHA-256, and extracted binary
SHA-256 from `upstream.lock.json`, then verifies both archive and executable before use. The mirror
contains the exact executable pinned from the private custom-Godot release, but is packaged as a
MochaScript release asset so Actions can fetch it with the job's built-in, read-only token. Local
verification hashes the resolved `godot` executable against the same binary pin. No custom secret,
personal credential, or mutable latest-release URL is required or embedded in the repository.

Godot resource identities are repository state. Every tracked `.gd`, `.mocha`, and `.gdextension`
file under `project/` must have its generated `.uid` sidecar committed. The canonical verifier fails
when one is missing, when an import leaves any untracked UID sidecar, or when editor import rewrites
a tracked sidecar. It hashes the Git-index versions and compares both isolated project copies against
that authority. Contributors should add new source resources and their sidecars together.

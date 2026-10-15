# Testing and CI

Run the complete local suite with:

```sh
./scripts/verify.sh
```

It validates immutable pins, uses the cgroup CPU quota with a four-job ceiling, builds debug and
release extension libraries, imports the fixture project, and runs these isolated sentinels exactly
once:

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

Linux CI reads the private release repository, immutable asset ID, archive SHA-256, and extracted
binary SHA-256 from `upstream.lock.json`, then verifies both archive and executable before use.
Local verification hashes the resolved `godot` executable against the same binary pin. Configure
`CUSTOM_GODOT_READ_TOKEN` as a read-only Actions secret. No credential or mutable latest-release URL
is embedded in the repository.

Godot resource identities are repository state. Every tracked `.gd`, `.mocha`, and `.gdextension`
file under `project/` must have its generated `.uid` sidecar committed. The canonical verifier fails
when one is missing or when an import leaves any untracked UID sidecar, so contributors should add
new source resources and their sidecars together.

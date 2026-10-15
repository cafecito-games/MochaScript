# Testing and CI

Run the complete local suite with:

```sh
./scripts/verify.sh
```

It validates immutable pins, uses the cgroup CPU quota with a four-job ceiling, builds the extension,
imports the fixture project, and runs these isolated sentinels exactly once:

- `MOCHA_RECOGNITION_OK`: language/resource identity and source preservation;
- `MOCHA_TOKENIZER_OK`: positive tokens and a stable lexical error;
- `MOCHA_FRONTEND_OK`: parser and analyzer positive/negative cases;
- `MOCHA_EXECUTION_OK`: compile, instantiate, host-object interaction, VM execution, and divergence
  canary;
- `MOCHA_NATIVE_OK`: direct C++ tokenizer/parser/analyzer/compiler/VM tests;
- `MOCHA_BOOTSTRAP_CORPUS_OK`: the pinned upstream baseline subset;
- `MOCHA_VERIFY_OK`: the aggregate result.

Any timeout, crash, engine error, missing or duplicate sentinel, unexpected diagnostic, or nonzero
exit fails verification. Logs are written under the user's cache rather than the repository.

Linux CI downloads release asset `542713790` from the private Cafecito custom-godot repository and
verifies SHA-256 `a9d5fb1a2998874cfc0da9100e690d3b76e46390fcf4da31585a70000a1e8ad6` before execution. Configure
`CUSTOM_GODOT_READ_TOKEN` as a read-only Actions secret. No credential or mutable latest-release URL
is embedded in the repository.

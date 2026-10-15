# Evolving MochaScript

Start every language change with one failing Mocha-owned test and update the specification or
divergence ledger in the same change.

## Adding syntax or semantics

1. Add tokenizer coverage if a new token is required. Keyword spellings live in the tokenizer's one
   keyword table and must also appear through the language integration API.
2. Add or extend an AST shape in `mocha_ast.h`, then parse it in `mocha_parser.cpp` with an `M1xxx`
   diagnostic for malformed input.
3. Define inferred and required types in `mocha_analyzer.cpp`; add an `M2xxx` diagnostic for invalid
   programs. Do not rely on Variant-only behavior to hide a missing rule.
4. Declare new opcodes or instruction fields in `src/runtime/mocha_bytecode.h`, then lower them in
   `mocha_compiler.cpp`. If execution is not ready, reject the construct with `M3xxx` rather than
   emitting a placeholder.
5. Implement runtime behavior in `mocha_vm.cpp`, including type and bounds checks and an `M4xxx`
   failure path.
6. Add native layer coverage plus a `.mocha` integration fixture when the behavior crosses the
   script-instance boundary. Run `./scripts/verify.sh`.

## Intentional divergence

Change the Mocha specification first, move any affected pinned parity fixture out of the baseline
or classify it as an expected difference, then implement against the Mocha test. Do not change the
implementation merely because a newer GDScript snapshot behaves differently. Use
`./scripts/review_upstream.sh` to inspect upstream work without applying it.

## Highest-value seams

The next useful expansions are lexical-scope metadata, full control-flow return analysis, typed
collection elements, index/member assignment, utility-function resolution through public ClassDB
metadata, then signals and async state. Script inheritance and editor completion should follow only
after cache/dependency semantics have a written design.

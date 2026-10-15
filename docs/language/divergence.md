# MochaScript divergence ledger

## Intentional Mocha behavior

### `mocha_language_version()` foundation canary

The zero-argument intrinsic returns `"MochaScript/bootstrap-1"`. It is intentionally absent from
GDScript. The canary is deliberately small: its purpose is to prove that a Mocha-owned rule can
cross parsing, static analysis, bytecode compilation, VM execution, and conformance testing without
delegating to or seeking approval from built-in GDScript. It is foundation metadata, not the
language's intended headline feature.

Calling it with arguments is diagnostic `M2007`.

This ledger is the language-design authority during the bootstrap. GDScript behavior is inherited
only when an entry or a MochaScript specification test says it is.

| Area | Status | MochaScript rule | Evidence |
| --- | --- | --- | --- |
| file extension | intentional divergence | Source files use `.mocha`. | recognition test |
| language identity | intentional divergence | Godot reports `MochaScript`, never `GDScript`. | recognition test |
| tokenizer and grammar | inherited subset | The pinned tokenizer plus the specified executable grammar are Mocha-owned. | native tests and pinned corpus |
| exponentiation precedence | inherited | `**` is left-associative; signed literals and explicit unary operators retain the pinned tokenizer/parser behavior. | native precedence tests |
| inferred declarations | intentional divergence | An untyped non-null initializer fixes the declaration's carrier type; later incompatible stores are `M2013`. | native analyzer test and pinned negative fixture |
| truthiness | intentional divergence | Conditions and logical operators require `bool`; arbitrary values are not truthy. | native analyzer tests |
| percent operator | intentional divergence | `%` is integer modulo only; String formatting is rejected with `M2003`. | native analyzer test |
| remaining static analysis | inherited subset | Declared types and the operations in the bootstrap specification are checked. | native and negative fixtures |
| compiler and runtime | intentional ownership | Mocha bytecode and VM execute scripts; GDScript is never called. | vertical slice |
| diagnostics | intentional divergence | Stable `M0xxx`–`M4xxx` records include source locations. | negative fixtures |
| global utility functions | unsupported | Unqualified unresolved calls target the host object; GDScript globals such as `print` are not resolved. | architecture boundary and corpus classification |
| language version intrinsic | intentional divergence | `mocha_language_version()` returns Mocha bootstrap metadata. | vertical slice canary |
| GDScript interoperability | unsupported | One Godot object has one script instance; Mocha does not inherit from or delegate to GDScript. | architecture boundary and instance test |

## Status vocabulary

- **inherited**: captured by a Mocha-owned specification test and currently matches the baseline.
- **intentional divergence**: deliberately differs from the baseline and is specified by Mocha.
- **planned inheritance**: expected bootstrap behavior but not yet proved in the port.
- **planned divergence**: an owned change with an identified verification surface.
- **unsupported**: must fail explicitly rather than degrade or delegate.

When a Mocha feature intentionally changes inherited behavior, move or replace the corresponding
bootstrap parity test. GDScript parity is never a reason to reject a specified Mocha behavior.

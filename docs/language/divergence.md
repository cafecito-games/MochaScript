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
| identifier policy | intentional divergence | Bootstrap identifiers are ASCII-only and reject non-ASCII text deterministically with `M0001`. | native tokenizer test |
| exponentiation precedence | inherited | `**` is left-associative; signed literals and explicit unary operators retain the pinned tokenizer/parser behavior. | native precedence tests |
| inferred declarations | intentional divergence | An untyped non-null initializer fixes the declaration's carrier type; later incompatible stores are `M2013`. | native analyzer test |
| static truthiness | intentional divergence | Statically known conditions, logical operands, and negations require `bool`. Dynamic `Variant` operands retain runtime truthiness in the bootstrap. | native analyzer tests |
| percent operator | intentional divergence | `%` is integer modulo only; String formatting is rejected statically with `M2003` or dynamically with `M4001`. | native analyzer and VM tests |
| shift semantics | intentional divergence | Shifts operate on signed 64-bit two's-complement bit patterns; left shift wraps, right shift is arithmetic, and counts outside `[0, 63]` report `M4001`. | native VM tests |
| collection element types | inherited syntax, bootstrap erasure | `Array[T]` and `Dictionary[K, V]` retain and validate type syntax, but element stores are not yet enforced at runtime. | native parser/analyzer tests |
| native property names | intentional divergence | Script members cannot shadow properties of their native base class or ancestors; collisions report `M2031`. | native analyzer test |
| VM safety budgets | intentional divergence | Each root entry receives one million bytecode operations and 128 nested Mocha frames; `M4001` aborts without rolling back prior effects. | native VM tests |
| remaining static analysis | inherited subset | Declared types and the operations in the bootstrap specification are checked. | native and negative fixtures |
| compiler and runtime | intentional ownership | Mocha bytecode and VM execute scripts; GDScript is never called. | vertical slice |
| diagnostics | intentional divergence | Stable `M0xxx`–`M4xxx` records include source locations. | negative fixtures |
| global utility functions | unsupported | Unqualified unresolved calls target the host object; GDScript globals such as `print` are not resolved. | specification and host-call architecture |
| language version intrinsic | intentional divergence | `mocha_language_version()` returns Mocha bootstrap metadata. | vertical slice canary |
| intrinsic and constant names | intentional divergence | The intrinsic and `PI`/`TAU`/`INF`/`NAN` names are reserved and cannot be shadowed. | native parser/analyzer tests |
| member initialization | intentional divergence | All carrier defaults are installed first and initializers run in declaration order, so current/later explicitly typed member reads observe defaults. Current or forward reads whose carrier still requires inference report `M2032`; `self` access and implicit-self calls before attachment report `M2030`. | native VM and analyzer tests |
| GDScript interoperability | unsupported | One Godot object has one script instance; Mocha does not inherit from or delegate to GDScript. | architecture boundary and instance test |

## Status vocabulary

- **inherited**: captured by a Mocha-owned specification test and currently matches the baseline.
- **intentional divergence**: deliberately differs from the baseline and is specified by Mocha.
- **planned inheritance**: expected bootstrap behavior but not yet proved in the port.
- **planned divergence**: an owned change with an identified verification surface.
- **unsupported**: must fail explicitly rather than degrade or delegate.

When a Mocha feature intentionally changes inherited behavior, move or replace the corresponding
bootstrap parity test. GDScript parity is never a reason to reject a specified Mocha behavior.

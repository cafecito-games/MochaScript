# MochaScript bootstrap specification

This document specifies the executable bootstrap subset. Anything not listed is unsupported unless
another Mocha-owned specification test says otherwise.

## Source and declarations

- Source files use `.mocha`; indentation forms blocks and `#` starts a comment.
- Bootstrap identifiers are ASCII-only; non-ASCII identifier characters fail lexically with
  `M0001`, independent of build type or active text services.
- A file may begin with `extends Type`; the default base is `RefCounted`.
- Top-level `var name[: Type] [= expression]` declares an instance member.
- `func name(parameters) [-> Type]:` declares an instance method; an omitted return type is
  `Variant`, while explicit `void` is supported only as a return type.
- Supported carriers are `Variant`, `bool`, `int`, `float`, `String`, `Array`, `Dictionary`, and
  Godot object class names. `Array[T]` and `Dictionary[K, V]` syntax is accepted, but its element
  types are deliberately erased and not yet enforced. Other carriers reject type arguments with
  `M1012`.

## Statements and expressions

Local variables, assignment and arithmetic compound assignment, `if`/`elif`/`else`, `while`, `return`,
`pass`, and expression statements execute. Expressions include literals, arrays, dictionaries,
indexing, member reads, calls, unary operators, arithmetic, comparison, logical, containment, and
bitwise operators. `self` names the host object. `PI`, `TAU`, `INF`, and `NAN` are built-in numeric
constants.
The constant names are reserved and cannot be redeclared (`M1013`); the contextual keywords
`match` and `when` are likewise reserved as declaration names (`M1014`).

Calls to another Mocha method execute in the Mocha VM. An unresolved unqualified call targets the
host Godot object through public `Variant::callp`; an explicit receiver calls that public object's
method. This is Godot object interoperability, not GDScript delegation.

`mocha_language_version()` is the intentional Mocha-only foundation canary described in the
divergence ledger. Its name is reserved; declaring a method with that name reports `M2029`.

## Static rules

Declared member, parameter, local, and return types are checked. `int` widens to `float`; unknown
`Variant` results remain dynamically checked at the storage or return boundary. Wrong external
argument types and wrong runtime stores fail explicitly. Integer-only operators (`%`, shifts, and
bitwise operations) also enforce integer operands when dynamic `Variant` values reach the VM.
Shifts use signed 64-bit, two's-complement bit patterns: left shift wraps discarded high bits,
right shift is arithmetic, and the shift count must be in `[0, 63]` or execution fails with
source-located `M4001`. These rules do not depend on the engine build type or C++ signed-shift behavior.

Untyped declarations infer and retain the initializer's static carrier type, so `var value = 1`
cannot later store a `String` (`M2013`). Statically known conditions, logical operators, and logical
negation require `bool` operands (`M2015`, `M2003`, and `M2002` respectively). A `Variant` operand
remains dynamic and currently uses Godot `Variant::booleanize()` at runtime. `%` and `%=` are integer
modulo operators, not String formatting (`M2003`). These strict static bootstrap rules are
intentional MochaScript divergences and may evolve only through the divergence workflow.

Typed declarations without an initializer receive carrier defaults (`false`, `0`, `0.0`, empty
`String`, empty `Array`, empty `Dictionary`, or `null` for objects). An untyped `null` initializer
remains `Variant` instead of creating an unusable null-only type. `and`/`or` and `&&`/`||`
short-circuit. Word-form `not` binds below comparison/containment and above `and`; symbolic `!`
remains a high-precedence unary operator.

Storage for every member is first populated with its carrier default, then initializers run in
declaration order. An unqualified read of an earlier member sees its initialized value; a read of
the current or a later member sees its carrier default. `self` is unavailable until Godot attaches
the completed script instance, so `self`-qualified access and unqualified calls through implicit
`self` are rejected statically with `M2030`. The self-free `mocha_language_version()` intrinsic is
the sole unqualified-call exception during initialization.

Exponentiation retains the pinned bootstrap baseline: `**` is left-associative. Signed numeric
literals remain tokenizer atoms (`-2 ** 2` is `4`), while an explicit spaced unary operator binds
below exponentiation (`- 2 ** 2` is `-4`). Like the VM carrier operation, an integer base and
integer exponent produce `int`; a float operand produces `float`.

The bootstrap frontend rejects recursively nested expressions, constructed expression trees, and
statement nesting deeper than 256 nodes with stable parser/analyzer diagnostics instead of risking
the native stack.

## Unsupported now

Classes, inheritance between scripts, constants, enums, signals, annotations, exports, properties,
static functions, lambdas, coroutines, `for`, `match`, `await`, `yield`, `break`, `continue`,
`assert`, `breakpoint`, pattern matching, enforcement of typed collection elements,
named/default/variadic arguments, setters, index/member assignment, super calls, global utility
functions such as `print`, the `is` type-test operator (`M2091`), preload/load semantics, RPC annotations, and editor code completion are
not implemented. Recognized unsupported statement forms report `M1090`; unsupported top-level
declarations report `M1008`; assignment to a non-name target reports `M2090`; compiler gaps report
an `M3xxx` diagnostic. No unsupported form may execute as a successful no-op.

The bootstrap parser is deliberately a coherent subset rather than a claim of full GDScript
parity. Pinned GDScript fixtures currently prove only tokenizer/parser/analyzer acceptance or
rejection; they are not runtime parity evidence. They will be reclassified whenever Mocha chooses
a different rule.

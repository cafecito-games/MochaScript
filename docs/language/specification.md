# MochaScript bootstrap specification

This document specifies the executable bootstrap subset. Anything not listed is unsupported unless
another Mocha-owned specification test says otherwise.

## Source and declarations

- Source files use `.mocha`; indentation forms blocks and `#` starts a comment.
- A file may begin with `extends Type`; the default base is `RefCounted`.
- Top-level `var name[: Type] [= expression]` declares an instance member.
- `func name(parameters) [-> Type]:` declares an instance method.
- Supported carriers are `Variant`, `bool`, `int`, `float`, `String`, `Array`, `Dictionary`, and
  Godot object class names. `Array[T]` and `Dictionary[K, V]` syntax is accepted, but its element
  types are deliberately erased and not yet enforced. Other carriers reject type arguments with
  `M1012`.

## Statements and expressions

Local variables, assignment and arithmetic compound assignment, `if`/`else`, `while`, `return`,
`pass`, and expression statements execute. Expressions include literals, arrays, dictionaries,
indexing, member reads, calls, unary operators, arithmetic, comparison, logical, containment, and
bitwise operators.

Calls to another Mocha method execute in the Mocha VM. An unresolved unqualified call targets the
host Godot object through public `Variant::callp`; an explicit receiver calls that public object's
method. This is Godot object interoperability, not GDScript delegation.

`mocha_language_version()` is the intentional Mocha-only foundation canary described in the
divergence ledger.

## Static rules

Declared member, parameter, local, and return types are checked. `int` widens to `float`; unknown
`Variant` results remain dynamically checked at the storage or return boundary. Wrong external
argument types and wrong runtime stores fail explicitly.

Untyped declarations infer and retain the initializer's static carrier type, so `var value = 1`
cannot later store a `String` (`M2013`). Conditions and both forms of logical operators require
`bool` operands (`M2015` and `M2003` respectively); there is no implicit truthiness. `%` is an
integer modulo operator, not String formatting (`M2003`). These strict bootstrap rules are
intentional MochaScript divergences and may evolve only through the divergence workflow.

Typed declarations without an initializer receive carrier defaults (`false`, `0`, `0.0`, empty
`String`, empty `Array`, empty `Dictionary`, or `null` for objects). An untyped `null` initializer
remains `Variant` instead of creating an unusable null-only type. `and`/`or` and `&&`/`||`
short-circuit. Word-form `not` binds below comparison/containment and above `and`; symbolic `!`
remains a high-precedence unary operator.

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
functions such as `print`, preload/load semantics, RPC annotations, and editor code completion are
not implemented. Recognized unsupported statement forms report `M1090`; unsupported top-level
declarations report `M1008`; assignment to a non-name target reports `M2090`; compiler gaps report
an `M3xxx` diagnostic. No unsupported form may execute as a successful no-op.

The bootstrap parser is deliberately a coherent subset rather than a claim of full GDScript
parity. Pinned GDScript fixtures currently prove only tokenizer/parser/analyzer acceptance or
rejection; they are not runtime parity evidence. They will be reclassified whenever Mocha chooses
a different rule.

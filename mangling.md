# Coda linker names

Coda keeps a symbol's source name separate from its linker name. `Symbol.name`
is used for source-level lookup and diagnostics; `Symbol.link_name` is used by
the backend when emitting symbol definitions and references.

## Identity

For mangled symbols, identity includes the symbol kind, module path, source
name, receiver type for methods, and concrete type arguments for a generic
specialization. Each component is encoded independently, so module separators
and punctuation inside type descriptions cannot make the encoding ambiguous.

The current encoding starts with `__coda1_`. The version is part of the format:
an incompatible future encoding must use a new version. Module path components,
the source name, receiver description, function signature, and each type argument
are represented as a byte length followed by the hexadecimal encoding of those bytes.

Symbol kind codes are `F` for functions, `V` for globals, `T` for types, and
`C` for constraints. The decoder validates the complete input and rejects
malformed or trailing data.

## Exceptions

`@extern` declarations preserve the declared foreign symbol name. The root
module's non-specialized `main` function also remains named `main` because the
current startup code calls that entry point directly. Ordinary top-level symbols
in an unnamed root module may remain unmangled. Tooling must accept raw names as
well as versioned Coda names.

## Tooling

Use `coda_demangle_symbol()` to recover module path, source name, receiver,
signature, and tagged generic argument descriptions from a linker name. Use
`coda_demangled_symbol_name()` for a readable qualified name. These descriptions
preserve symbol identity; locating a declaration or source range still requires
the module/source index.

The generated function's internal compiler name may remain an ordinal such as
`.L_coda_generic_0`; that is not the emitted linker name. The linker name is
derived from the generic declaration and its concrete type arguments, so it is
stable across discovery order and can be demangled by tooling.

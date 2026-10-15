# Coda linker names

Coda stores source names and linker names separately. `Symbol.name` remains the
source-level identifier; `Symbol.link_name` is used by the backend when emitting
symbol definitions and references.

## Format

A qualified name is encoded as `_C` followed by its source-name components.
Each component is prefixed by its byte length, with no separators between
components. For example, `std::io::print` becomes `_C3std2io5print`.
The final component is the declaration name; preceding components are its module
path. This is the same compact, readable encoding used by the earlier compiler.

Method names append `_R` followed by a length-prefixed receiver type. Generic
specializations append `_G` plus the number of type arguments; each type argument
is prefixed by `T` and its byte length. For example, `std::io::identity<int32>`
becomes `_C3std2io8identity_G1T5int32`.

Type payloads preserve ASCII letters and digits, while punctuation, whitespace,
and underscores are escaped as `_HH` byte sequences. This keeps emitted labels
assembler-safe without hex-encoding ordinary names or even simple type arguments
such as `int32`. Only type arguments are currently encoded; value-generic
arguments are not supported by the current compiler.

Declaration kinds and ordinary function signatures are not repeated in the
encoding: names are unique within a module namespace, and current method lookup
uses the receiver type. The demangler therefore reconstructs a qualified identity,
not a complete declaration record; tooling should resolve the result against its
symbol index to obtain declaration kind and source location. If overloads are
introduced later, add only the discriminator they require.

`@extern` declarations preserve their foreign ABI names. The root `main` entry
point also remains `main` while the current startup code expects that symbol;
this is a temporary ABI constraint, not part of the general naming scheme.

`coda_demangle_symbol()` parses compact names into their qualified components,
receiver, and tagged generic arguments. Raw `@extern` names are not Coda-mangled
names and cannot be decoded by this function.

# Coda linker names

Coda keeps source names separate from linker names. `Symbol.name` remains the
source-level identifier; `Symbol.link_name` is used when the backend emits
symbol definitions and references.

## Symbol format

A qualified symbol starts with `_C`, followed by its length-prefixed source-name
components. Components are concatenated without separators. The final component
is the declaration name; preceding components form its module path.

```text
std::io::print       -> _C3std2io5print
std::io::identity    -> _C3std2io8identity
```

A method adds `_R` followed by one structural type encoding for its receiver.
A generic specialisation adds `_G<count>_`, followed by exactly `count`
structural type encodings. Type encodings are self-delimiting, so they do not
need an extra textual length prefix or a `T` marker.

```text
std::io::identity<int32> -> _C3std2io8identity_G1_B5int32
```

The root `main` entry point remains unmangled as `main` while the current startup
code expects that symbol. This is an ABI constraint, not a general naming rule.
`@extern` declarations preserve their exact foreign linker names.

## Structural type format

All structural encodings use assembler-safe ASCII. Source-name components are
length-prefixed in bytes, so underscores in identifiers require no escaping.
Nominal types are encoded by identity rather than by expanding their underlying
layout.

| Encoding | Meaning |
| --- | --- |
| `B<len><name>` | Built-in type, such as `B6uint64` |
| `N<count>_<components>[_I<count>_<types>]` | Nominal type and optional generic type arguments |
| `V<index>` | Generic type parameter reference, such as `V0` |
| `P<type>` | Non-optional pointer |
| `Q<type>` | Optional pointer |
| `L<type>` | Slice |
| `A<length>_<type>` | Array with a fixed length |
| `S<count>_<types>` | Sum type with `count` members |
| `F<count>_<parameters><return-type>` | Function type |
| `C<count>_<field-name><type>...` | Structural `struct` with named fields |
| `U<count>_<field-name><type>...` | Structural `union` with named fields |
| `E<type>` | Anonymous enum represented by its underlying type |

A component is encoded as its byte length immediately followed by its bytes.
For nominal types, the component count includes the final type name. For example,
`std::fs::FileError` becomes `N3_3std2fs9FileError`. Generic type arguments
on a nominal type use `_I<count>_`; this is distinct from `_G`, which introduces
a function specialisation.

Counts are decimal. The underscore after a count separates it from the
length-prefixed component sequence or the next structural type. Recursive type
encodings determine their own extent, so concatenated types remain unambiguous.

## Generic receiver patterns

When a generic function's receiver type can be represented by its supported
structural pattern, references to its declared type parameters use `V<index>`.
For example, a receiver declared as `(T | E)` with generic parameters `T, E`
uses `S2_V0V1`, rather than encoding the concrete receiver types again. The
specialisation then carries the concrete types once in `_G<count>_`.

Receiver patterns containing unsupported generic constructions fall back to an
encoding of the resolved receiver type. This keeps the name deterministic and
correct even when the pattern encoder cannot preserve the original generic
parameter structure.

## Declaration identity and future changes

Declaration kinds and ordinary function signatures are not encoded. Names are
unique within a module namespace under the current language rules, and method
identity includes its receiver type. The demangler reconstructs a qualified
name, receiver, and generic arguments—not a complete declaration record.
Tooling should resolve the demangled identity against its symbol index to find
declaration kind and source location. If overloads are introduced, add only the
discriminator needed to distinguish otherwise identical declarations.

`coda_demangle_symbol()` parses Coda-mangled names and formats structural type
encodings for display. Raw `@extern` names and the unmangled root `main` are not
Coda-mangled names and therefore cannot be decoded by this function.

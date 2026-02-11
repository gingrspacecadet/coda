# CODA
A fresh attempt at a systems language, without all that clutter!

## Core principles
- Immutability and privacy by default
- Shadowing is forbidden at all scopes (module and local). Redeclaration of an existing name is a hard error.
- There are no warnings, only errors (aka `-Wall -Wextra -Werror` and you CANNOT turn it off :3) 

## Safety and UB
Coda is designed in such a way that undefined behaviour (UB) is intentionally difficult to do. Anything that could potentially cause it **must** be wrapped in an `unsafe` block. Unitialised reads are compile-time errors, integer overflow is defined (wrap), implicit integer <-> pointer casts are forbidden, and pointer arithmetic is `unsafe`.

There are also some optional assistances to keep code clean and safe, for example `@null` which inserts runtime checks for null dereferences.

## Null shenanigans
`?` can be used when declaring a pointer type to specify whether the pointer can or can not be null. Without it, the compiler will throw an error if there is any possibility of a null dereference, otherwise it will ignore

## Builtin types
- `word` - native platform word size. Is the exact size of a pointer. 
- `char` - QOL type alias for `int8`
- `[u]int(8|16|32|64)` - fixed-width integers
- `bool` - logically boolean, but stored in one byte for ABI compatibility (unless a struct is `@packed` or for explicit bitfields)
- `string` - A builtin structure containing the pointer and the length (aka an array with runtime known width)
- `err` - return type alias for functions that return status
  - `OK` - always 0
  - `ERROR` - every occurence in a function body in order is assigned a sequential integer starting at 1
- `int` - QOL alias for `int32`
- `uint` - QOL alias for `uint32`
- `null` - pointer to `0x0`

> [!NOTE]
> Integer overflow uses two's complement wrapping semantics (% MAX). The compiler will not assume overflow is impossible for optimisation

## Attributes
All attributes are denoted by a `@` preceeding the name of the attribute

### Layout attributes
- `@packed` - disabled all potential struct padding

### Safety attributes
- `@unsafe` - marks a function as unsafe to call; it must be called from a corresponding `unsafe` block.

### Linkage attributes
- `@extern` - disables name mangling for the declared symbol and requests the platform C calling convention. Can be used on declarations or on definitions. The compiler will treat the symbol as linker-provided unless a body is present; the emitter must map the symbol name exactly.

## Modules
Every source file is a module
- `module [name];` - must be on the first non-empty line of the file.
- `include [module];` - brings said (sub)module into scope. Must be directly after the module definition

## Includes
- `include` imports another module's exported symbols
- `include foo as bar` - potential module aliasing (debatable)

## Variable definition

### Standard definition:
```
int foo = 9;
```

### Mutability
```
char *mut input;      // mutable pointer to immutable data
mut char *input;      // immutable pointer to mutable data
mut char *mut input;  // mutable pointer to mutable data
```
This also makes deeper pointer chains more easily readable:
```
mut char **mut *ptr;  //const->mut->const->mut
```

## Function syntax

Functions must always start with `fn`, followed by the return type, then the name and finally a list of parameters:
```
fn word pci_read_word(word dev, word bus, word func, word addr);
```
As always, parameters are immutable by default.  
Any array types passed or specified in the arguments immediately decay into raw pointers.  
Functions defined with the return type `err` will always return `OK` (0) on success, and `ERROR` (!0) on failure.

## Arrays
Sizes are compile-time constants, used for pre-allocating room on the stack for a known amount of data.  
We do not support dynamic arrays (for now!)

Notes:
* Sizes are compile time constants
* Arrays decay to pointers only when passed to parameters that are declared as pointers

## Scalar-based type casting
Casting a type to another type with a greater width is allowed implicitly, whilst the inverse is not (it must be explicitly casted).

Reads of any storage that are not provably initialised are compile time errors. To get aroudn this in very rare occasions, enclose it in an `unsafe` block.

## Pointer safety
Any pointer arithmetic is considered "unsafe" and will throw an error unless it is within an `unsafe {}` code block.  
Explicit `unsafe` is required for:
- pointer arithmetic (except for arrays with known bounds)
- integer <-> pointer casts  
The compiler can and will assume that all pointers are not `null` unless explicitly marked with the `@null` parameter.

## `export` semantics
Only exported symbols are accessible from other modules

### name mangling
- By default, the compiler will mangle module-local symbol names to avoid collisions (eg include the module path in the symbol)
- `@extern` removes manglign and requires the platform's C calling convention to be used
- varargs are a WIP

## Enums
Enums are defined in this way:
```
type enum name {
  a = 0,
  b = 2,
  c, // implicit increment from previous value `b`
};
```

## Inline assembly
Uses similar syntax to GnuC:
```
asm("asm string", inputs, outputs, constraints);
```

inputs use the following syntax:
- "reg": set the specified register to a variable
- "int": set the corresponding "%int" inside the assembly block to a variable

outputs use the same syntax.

constraints are lists of registers that may be modified.

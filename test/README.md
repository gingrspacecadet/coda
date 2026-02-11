# stuffs

## Builtin types
`word` - native platform word size
`char` - exactly the size of an ascii character
`[u]int(8|16|32|64)` - fixed-width integers
`bool` - a single bit
`string` - type alias for `char*` (maybe include length storage, not sure)
`err` - return type alias for functions that return status
- `OK` - always 0
- `ERROR` - increments by 1 for each consecutive call in-scope (allows for very simple error tracing)
`int` - QOL alias for `int32`
`uint` - QOL alias for `uint32`
`null` - pointer to `0x0`

## Attributes
All attributes are denoted by a `@` preceeding the name of the attribute
`@packed` - disabled all potential struct padding
`@null` - enables automatic `null``-dereference checking
`@extern` - undefined symbol to be located during linking

## Modules
Every source file is a module
`module [name]` - must be on the first non-empty line of the file.
`include [module]` - brings said (sub)module into scope. Must be directly after the module definition

## Includes
`include` imports another module's exported symbols
`include foo as bar` - potential module aliasing (debatable)

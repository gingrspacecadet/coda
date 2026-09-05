# x64_64/Linux std::fmt

## Purpose

`std::fmt` provides the standard formatting facilities.

It is responsible for converting values into textual representations and writing them to an output sink.

It does not:
* perform I/O directly
* own output streams
* allocate (unless explicitly requested)
* define how streams operate

`std::fmt` operates on `std::io::Writer`s.

## Design goals

The formatting system should:
* support arbitrary user-defined types
* require no boilerplate for simple types
* produce useful output by default
* avoid unnecessary alocations
* support writing directly to arbitrary output sinks
* use reflection where appropriate
* remain deterministic

## Formatting pipeline
Formatting proceeds in four stages.
1. Parse the format string
2. Match placeholders to arguments
3. Convert each argument into text
4. Write text to the supplied `Writer`.

## Public API

Core functions

```coda
fn none write(
    mut io::Writer writer,
    string format,
    any[] args
);

fn string format(
    mem::Allocator alloc,
    string format,
    any[] args
);

fn uint64 measure(
    string format,
    any[] args
);
```
Future additions may include:
```
fn none writeln(...);
fn string debug(...);
fn string display(...);
```

## Format string grammar
Initial grammar is intentionally small.

Supported placeholders:
```
{}
```
Escaped braces:
```
{{

}}
```
Arguments are consumed in order.
Example:
```
"{} {} {}"
```
Later versions may add:
```
{:x}
{:08}
{:<10}
{:>10}
{:.3}
```
but are not currently designed.

## Automatic formatting
Every value in coda is printable

The formatter chooses the representation in this order.

### 1. Custom formatter
If the type implements:
```
Display
```
use it.

Otherwise...

### 2. Debug formatter
If the type implements:
```
Debug
```
use it.

Otherwise...

### 3. Reflection
Use runtime type information.

This guarantees every type has a printable representation.

## Primitive formatting
Default representations.

Boolean
```
true
false
```

Integers
```
123
-5
```

Floating point
```
3.14
```

Characters
```
'a'
```

Strings
```
"hello"
```

Pointers
```
0x7ffabc...
```

### Compound types
Arrays
```
[1, 2, 3]
```

Slices
```
[1, 2, 3]
```

Structs

Reflection prints field names.
```
Point {
    x: 10,
    y: 20,
}
```

Enums
```
Colour::Red
```

### Recursive structures
The formatter must detect cycles.

Example:
```
Node {
    next: <cycle>
}
```
, rather than recursing forever.

## Reflection
`any` provides:
* runtime type
* value access
* field enumeration
* interface discovery
The formatter relies entirely on this metadata

No compiler special cases should exist.

## Output guarantees
`write()`:
* writes directly to the supplied `Writer`
* performs no implicit allocation
* may issue multiple `Writer.write()` calls
`format()`:
* allocates exactly one output string
* uses the supplied alocator
* never uses global allocation

## Errors
Formatting errors are distinct from I/O errors.

Examples:
* Invalid format string
* placeholder mismatch
* unsupported format specifier
Writer failuers originate from the output sink.

These should remain distinguishable.

## Display interface
```
interface Display {
    fn none format(
        mut io::Writer writer
    );
}
```
Purpose:

Produce concise, user-facing output.

Example:
```
3 + 4i
```

## Debug interface
```
interface Debug {
    fn none debug(
        mut io::Writer writer
    );
}
```
Purpose:

Produce verbose, programmer-facing output.

Example:
```
Complex {
    real: 3,
    imag: 4
}
```

## Design principles
1. Every value is printable
2. Reflection provides the default implementation
3. Custom formatters override reflection
4. Formatting is independent of output destination
5. Formatting does not imply allocation
6. the format never depends on `std::fs`, `std::process`, `stdout`, etc.
7. `std::fmt` is the only standard library module responsible for texture representation.

## Future extensions
Once the core is stable, this module can be extended with:
* width and alignment
* precision
* hexadecimal, octal, binary formatting
* floating-point formatting modes
* named arguments
* colour support
* localisation
* ANSI styling
* compile-time format string validation
* custom formatter registration
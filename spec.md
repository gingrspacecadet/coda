# Coda Language Specification

## Table of Contents

1. Overview
2. Lexical Structure
3. Modules
4. Types
5. Declarations
6. Expressions
7. Statements
8. Generics
9. Error Handling
10. Attributes
11. Intrinsics
12. Memory Model

## 1. Overview

Coda is a statically typed systems programming language designed around explicitness, safety, and predictable compilation.

Coda provides:

* Static typing
* Explicit mutability
* Manual memory management
* First-class pointers
* Generic programming
* Compile-time evaluation and introspection
* Algebraic error handling
* A C-compatible compilation model

Coda programs are compiled ahead of time into native machine code.

The language avoids implicit behaviour where possible. Operations which may affect ownership, mutability, memory, or control flow are explicit in source code.

## 3. Modules

Every Coda source file belongs to exactly one module.

A module declaration is written using the `module` keyword:

```coda
module example;
```

A module defines the namespace containing all declarations in the file.

Declarations are private by default. A declaration can be made visible outside its module using the `@export` attribute.

Example:

```coda
@export
fn int main(string[] args) {
    return 0;
}
```

Modules may include other modules using `include`.

Example:

```coda
include std::io;
```

Included symbols are added to the current namespace using the fully qualified module path by default.

An alias may be specified:

```coda
include std::io = io;
```

This makes symbols available through the alias:

```coda
io::println("Hello");
```

## 4. Types

Coda is a statically typed language. Every expression and value has a compile-time type.

Types describe the representation, allowed operations, and behaviour of values.

Coda types are divided into:

* Primitive types
* Compound types
* User-defined types
* Function types
* Generic types

### 4.1 Primitive Types

Coda provides the following built-in primitive types.

#### Integer Types

Signed integers:

```coda
int8
int16
int32
int64
```

Unsigned integers:

```coda
uint8
uint16
uint32
uint64
```

The size of an integer type is fixed and does not depend on the target platform.

#### Boolean Type

The `bool` type represents a boolean value.

A boolean may contain one of two values:

```coda
true
false
```

#### Character Type

The `char` type represents an 8-bit character value.

`char` is equivalent to:

```coda
uint8
```

#### String Type

The `string` type represents a sequence of characters.

String literals have type `string`.

Example:

```coda
string message = "Hello";
```

The `string` type is an alias to the `char[]` type.

Strings are not null-terminated unless explicitly created that way.

### 4.2 Type Aliases

A type alias introduces another name for an existing type.

Syntax:

```coda
type Name = ExistingType;
```

Example:

```coda
type byte = uint8;
```

An alias does not create a new type. Values of the original type and the alias type are interchangeable.

### 4.3 Pointers

A pointer stores the address of another value.

Syntax:

```coda
T *
```

Example:

```coda
int *ptr;
```

Pointers may be mutable or immutable.

A mutable pointee is written using `mut` after the pointee type:

```coda
int mut *ptr;
```

An optional pointer is written using `?`:

```coda
int *? ptr;
```

A non-optional pointer will never be `null`.

The only pointer type that may have the value `null` is an optional pointer.

### 4.4 Array Types

Arrays contain multiple values of the same type.

Fixed-size arrays use the syntax:

```coda
T[n]
```

Example:

```coda
uint8[4]
```

Arrays contain:

* Their length (`uint64`)
* Their data

This represents four consecutive `uint8` values.

Arrays own their data.

### 4.5 Slice Types

A slice represents a dynamically sized sequence of values.

Syntax:

```coda
T[]
```

Example:

```coda
uint8[]
```

Slices contain:

* Their length (`uint64`)
* A pointer to the first element

Slices do not own their referenced data.

### 4.6 Function Types

Functions are first-class types.

Syntax:

```coda
fn ReturnType(Parameters)
```

Example:

```coda
fn int(uint8)
```

Function pointers are declared using pointer syntax:

```coda
fn int(uint8) *
```

### 4.7 Mutability

All values are immutable by default.

The `mut` qualifier declares that the value immediately following it is mutable.

Example:

```coda
int value = 10;
mut int count = 0;
```

Pointers contain two independent mutable properties:

1. The mutability of the pointer itself.
2. The mutability of the referenced value.

The position of `mut` determines which property is affected.

Examples:

```coda
int *ptr;
```

An immutable pointer to an immutable value.

```coda
int mut *ptr;
```

A mutable pointer to an immutable value.

```coda
mut int *ptr;
```

An immutable pointer to a mutable value.

```coda
mut int mut *ptr;
```

A mutable pointer to a mutable value.

## 5. Declarations

Declarations introduce named entities at module scope.

Coda has three kinds of declarations:

- Global variable declarations
- Function declarations
- Type declarations

Declarations are private by default. A declaration may be made visible outside its module by applying the `@export` attribute.

Example:

```coda
@export
fn int main(string[] args) {
    return 0;
}
```

Only exported declarationss may be accessed from other modules.

### 5.1 Global Variable Declarations

A global variable declaration introduces a variable with module-wide lifetime.

Syntax:

```coda
Type name = expression;
```

Example:

```coda
uint32 port = 8080;
```

Global variables must always be initialised.

The initialiser must either:

- Evaluate to a compile-time value
- Be assigned through a runtime start routine

Compile-time initialisation is performed during compilation and does not require runtime execution.

```coda
uint32 buffer_size = 1024 * 1024;
string path = $string::concat(#file(), "/main.coda");
```

Runtime initialisation is performed before the program entry point is called.

The order of runtime global intialisation is defined by the module dependency order.

### 5.2 Function Declarations

A function declaration introduces a named function.

Syntax:

```coda
fn ReturnType name(Parameters) {
    body
}
```

Example:

```coda
fn int add(int a, int b) {
    return a + b;
}
```

Functions are values with function types and may be passed as arguments or stored in variables.

A function declaration may omit its body when the implementation is provided externally.

Example:

```coda
@extern
fn none foo();
```

### 5.3 Type Declarations

A type declaration introduces a named type.

Syntax:

```coda
type Name = Type;
```

Example:

```coda
type byte = uint8;
```

The right-hand side of a typedeclaration may be any valid type expression.

Examples:

```coda
type Buffer = uint8[];

type Vec3 = struct {
    int x;
    int y;
    int z;
};
```

Type declarations do not introduce runtime values.

## 6. Expressions

### 6.x Compound Operators

Compound operators are syntactic sugar for existing expressions.

They do not introduce new runtime operations.

Logical compound operators:

| Operator | Equivalent  |
| -------- | ----------- |
| `a !& b` | `!(a && b)` |
| `a !\| b` | `!(a \|\| b)` |

Bitwise compound operators:

| Operator | Equivalent |
| -------- | ---------- |
| `a ~& b` | `~(a & b)` |
| `a ~\| b` | `~(a \| b)` |
| `a ~^ b` | `~(a ^ b)` |

Compound assignment operators are equivalent to assigning the result of the corresponding expression:

```coda
a !&= b;
```

is equivalent to:

```coda
a = !(a && b);
```

The left-hand side of a compound assignment is evaluated exactly once.

# Coda Language Specification

## Table of Contents

0. Overview
1. Design Principles
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

## 0. Overview

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

## 1. Design Principles

Coda is designed around a few core principles.

### Explicit over Implicit

Coda avoids hidden behaviour where possible.

Operations which may affect program behaviour should be visible in the source code.

Examples:

- Memory allocation is explicit
- Errors are values
- Mutability is declared
- Imports are explicit

The compiler should not make surprising decisions on behalf of the programmer.

### Simple language, powerful compiler

The language should provide a small number of orthogonal features that compose well.

Complex behaviour should come from:

- compile-time evaluation
- generic programming
- modules
- libraries

rather than from many special cases built into the language.

### No hidden costs

Code should make its performance characteristics understandable.

A programmer should be able to determine:

- where allocations happen
- where copies happen
- where control flow can exit
- what operations may be expensive

Convenience features should not hide significant runtime costs.

### Safety through rules, not restrictions

Coda should provenet common mistakes while remaining suitable for systems programming.

The compiler should reject ambiguous or unsafe constructs, but should not prevent low-level programming.

### Libraries, not language features

Functionality should live in libraries wherever possible.

The compiler should provide mechanisms such as:

- generics
- compile-time execution
- interfaces 
- methods

and the standard library should build upon them.

### One obvious way

Coda prefers one clear solution over many equivalent mechanisms.

Examples:

- Modules are the namespace boundary
- Errors use inline sum types
- Mutability uses `mut`

The language should avoid requiring programmers to remember subtle rules.

### Predictable compilation

The compiler should behave consistently.

A programmer should be able to understand why code compiles or fails.

Coda does not use warnings as a separate category of correctness. Invalid code should be rejected.

### Zero-cost abstractions

High-level features should compile down to efficient low-level code.

Generics, interfaces, and convenience syntax should not require unnecessary runtime overhead unless explicitly requested.

### The programmer controls resources

Resources should have clear ownership.

The programmer controls:

- memory allocation
- file handles
- system resources
- lifetimes

The language should make ownership visible rather than hiding it behind automatic mechanisms.

## 2. Lexical Structure

A Coda source file consists of a sequence of Unicode code points organised into tokens and separated by whitespace or comments.

The lexical structure of a program is independent of its meaning.

### 2.1 Source Encoding

Coda source files are encoded using UTF-8.

Implementations must reject source files that are not valid UTF-8.

### 2.2 Whitespace

Whitespace separates tokens but otherwise has no semantic meaning.

The following characters are considered whitespace:

- Space (`U+0020`)
- Horizontal tab (`U+0009`)
- Line feed (`U+000A`)
- Carriage return (`U+000D`)

Whitespace may appear between any two tokens unless prohibited by the grammar.

### 2.3 Comments

Coda supports both line comments and block comments.

A line comment begins with `//` and continues until the end of the current line.

Example:

```coda
// This is a comment.
```

A block comment begins with `/*` and ends with `*/`.

Example:

```coda
/*
    This is a block comment.
*/
```

Comments are treated as whitespace.

Nested block comments are permitted.

### 2.4 Identifiers

Identifiers name declarations.

Identifiers use ASCII only.

An identifier consists of a leading ASCII letter or underscore, followed by zero or more ASCII letters, digits, or underscores.

Unicode must not be accepted in identifiers.

Examples:

```coda
foo
_bar
Vec3
count1
```

Identifiers are case-sensitive.

The following are distinct identifiers:

```coda
count
Count
COUNT
```

### 2.5 Keywords

Keywords are reserved identifiers with special meaning.

The following keywords are reserved by the language:

```coda
module
include
type
struct
union
enum
fn
return
if 
else 
match 
for 
while 
break 
continue 
defer 
mut 
true
false 
null 
none 
any
```

Reserved keywords may not be used as identifiers.

### 2.6 Attributes

An attribute begins with the `@` character followed immediately by an identifier.

Examples:

```coda
@export
@extern
@packed
```

The meaning of attributes is defined in the Attributes chapter.

### 2.7 Compiler Intrinsics

A compiler intrinsic begins with the `#` character followed immediately by an identifier.

Examples:

```coda
#file
#line
#typeid
```

The meaning of compiler intrinsics is defined in the Intrinsics chapter.

### 2.8 Compile-Time Evaluation

The `$` token denotes compile-time evaluation.

It may precede either an expression or a statement.

Examples:

```coda
$value

$function()

${
    generate_table();
}
```

The semantics of compile-time evaluation are defined in the Expressions chapter.

### 2.9 Literals

The lexical forms of literals are:

- Integer literals
- Floating-point literals
- Character literals
- String literals
- Boolean literals
- The `null` literal

The semantic interpretation of literals is defined in the Expressions chapter.

### 2.10 Integer Literals

Integer literals represent integral values.

Integer literals are initially untyped.

The type of an integer literal is determined by the surrounding context.

#### Decimal Literals

Decimal literals consist of one or more decimal digits.

Examples:

```coda
0
1
42
123456789
```

#### Hexadecimal Literals

An hexadecimal literal begins with the prefix `0x`.

The remaining digits must be hexadecimal digits.

Examples:

```coda
0xFF
0x1234
0xDEADBEEF
```

#### Binary Literals

A binary literal begins with the prefix `0b`.

The remaining digits must be binary digits.

Examples:

```coda
0b0
0b1
0b10101010
```

#### Octal Literals

An octal literal begins with the prefix `0o`.

The remaining digits must be octal digits.

Examples:

```coda
0o755
0o644
```

#### Digit Separators

Underscores (`_`) may appear between digits to improve readability.

Digit separators do not affect the value of the literal.

Examples:

```coda
1_000_000
0xDEAD_BEEF
0b1111_0000
0o123_456
```

A digit separator must not:

- Appear at the beginning of a literal
- Appear at the end of a literal
- Appear immediately after the radix prefix
- Appear consecutively

The following are ill-formed:

```coda
_123
123_
0x_FF
1__000
```

#### Unary negation

Negative integers are formed by applying the unary `-` operator to an integer literal.

`-42` is not a literal, but the expression:

```coda
-(42)
```

This rule applies uniformly to all integer literals.

### 2.11 Floating-Point Literals

Floating-point literals represent real-valued numbers.

Floating-point literals are initially untyped.

A floating-point literal contains either:

- A decimal point, or
- An exponent marker

Examples:

```coda
0.0
1.5
3.
1e6
6.022e23
```

A floating-point literal may contain an optional sign only as part of an exponent.

Digit separators may be used in floating-point literals in the same manner as integer literals.

### 2.12 Character Literals

Character literals represent a single `char` value.

A character literal is enclosed in single quotes.

Examples:

```coda
'a'
'Z'
'\n'
'\x41'
```

Character literals may use escape sequences.

A character literal must evaluate to exactly one character value.

### 2.13 String Literals

String literals represent a sequence of characters.

A string literal is enclosed in double quotes.

Examples:

```coda
"Hello"
"line 1\nline 2"
""
```

String literals may use escape sequences.

String literals have type `string`.

The contents of a string literal are encoded as a sequence of `char` values.

### 2.14 Escape Sequences

Escape sequences may appear in character literals and string literals.

Coda supports the following escape sequences:

- `\\`
- `\"`
- `\'`
- `\n`
- `\r`
- `\t`
- `\0`
- `\xNN`

Where `NN` denotes two hexadecimal digits.

The `\xNN` escape denotes a single byte value.

### 2.16 Boolean and Null Literals

The boolean literals are:

```coda
true
false
```

The null literal is:

```coda
null
```

`null` is not a general integer literal. It is a dedicated literal token with meaning defined by the type system.

### 2.17 Tokenisation Notes

The lexer must choose the longest valid token at each position.

For example:

- `>>=` is a single token, not `>>` followed by `=`
- `!&=` is a single token, not `!` followed by `&=`
- `0xFF` is a single integer literal token

Whitespace and comments separate tokens but otherwise have no semantic meaning

### 2.18 Operators and Punctuation

Coda defines the following punctuation tokens:

```coda
(
)
[
]
{
}
.
,
:
;
::
->
?
```

Coda defines the following operator tokens:

```coda
+
-
*
/
%
&
|
^
~
!
&&
||
==
!=
<
<=
>
>=
=
+=
-=
*=
/=
%=
&=
|=
^=
<<
>>
<<=
>>=
!&
!|
~&
~|
~^
!&=
!|=
~&=
~|=
~^=
```

Additional operators may be introduced by future revisions of the language.

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

A mutable pointer is written using `mut` after the pointee type:

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

Only exported declarations may be accessed from other modules.

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

The order of runtime global initialisation is defined by the module dependency order.

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

The right-hand side of a type declaration may be any valid type expression.

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

An expression produces a value.

Every expression has a compile-time type.

Expressions may be used wherever a value is required, including:

- Variable initialisers
- Function arguments
- Return values
- Conditions
- Assignments

Expressions are evaluated according to the rules of their operators and subexpressions.

Unless otherwise specified, evaluation order is left-to-right.

### 6.1 Literals

Literals represent values directly written in source code.

Coda provides:

- Integer literals
- Floating-point literals
- Boolean literals
- Character literals
- String literals
- Null literals

#### Integer Literals

Integer literals may be written in decimal form.

Example:

```coda
uint32 value = 100;
```

Integer literals have an untyped integer type. An untyped integer literal may be implicitly coerced into an integer type if the value fits within the destination type.

Examples:

```coda
uint8 a = 5;
int8 b = -2;
```

The following conversions are not permitted implicitly:

```coda
uint32 c = -4;    // unsigned integers cannot be negative
uint8 d = 999999;  // value does not fit in uint8
```

Implicit integer conversion is only permitted when no truncation or sign change occurs.

#### Boolean Literals

Boolean literals are:

```coda
true
false
```

#### Character Literals

Character literals represent a single `char` value.

Example:

```coda
char c = 'A';
```

#### String literals

String literals have type `string`

Example:

```coda
string message = "Hello";
```

#### Null Literals

The `null` literal denotes the zero value for optional pointer types.

`null` may only be assigned to an optional pointer type.

### 6.2 Identifiers

An identifier expression refers to a declared entity.

Example:

```coda
count
```

The referenced entity must be visible in the current scope.

Identifiers may refer to:

- Variables
- Functions
- Types

### 6.3 Operators

Operators are built-in syntax for combining or transforming values.

Coda does not support operator overloading.

The compiler may expose intrinsic operations such as `#add`, which may call a type-specific method such as `.add`, but ordinary operators are not overloadable.

#### Arithmetic Operators

The arithmetic operators are:

- `+`
- `-`
- `*`
- `/`
- `%`

#### Bitwise Operators

The bitwise operators are:

- `&`
- `|`
- `^`
- `~`
- `<<`
- `>>`

#### Logical Operators

The logic operators are:

- `!`
- `&&`
- `||`

`&&` and `||` short-circuit.

#### Comparison Operators

The comparison operators are:

- `==`
- `!=`
- `<`
- `<=`
- `>`
- `>=`

Comparison operators are binary only. Chained comparisons are not special syntax.

Example:

```coda
a < b < c
```

is equivalent to:

```coda
(a < b) < c
```

and is invalid unless the intermediate result is valid for the second comparison.

#### Assignment Operators

Assignment produces a value.

Example:

```coda
a = b;
```

The result of an assignment expression is the assigned value.

Compound assignments are also expressions and produce a value.

### 6.4 Compound Operators

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

### 6.5 Casts

A cast explicitly converts a value to another type.

Syntax:

```coda
(T)expression
```

Example:

```coda
uint8 x = (uint8)big;
```

Implicit casts are permitted only when the destination type can represent every value of the source type.

This means:

- widening integer conversions are permitted
- narrowing integer conversions are not permitted implicitly
- signedness changes are not permitted implicitly

Examples:

```coda
uint8 a = 5; 
uint32 b = a; // permitted 

uint32 c = 5; 
uint8 d = c; // requires explicit cast 

int32 e = 5; 
uint32 f = e; // requires explicit cast
```

### 6.6 Function Calls

A function call invokes a function value.

Syntax:

```coda
callee(arguments)
```

Example:

```coda
add(1, 2)
```

Arguments are evaluated in source order unless otherwise specified.

### 6.7 Member Access

Member access selects a field or method-like member.

Syntax:

```coda
value.name
```

Pointer dereferences may use arrow syntax:

```coda
ptr->name
```

This is shorthand for dereferencing the pointer then applying member access.

### 6.8 Indexing

Indexing selects an element from an array or slice.

Syntax:

```coda
value[index]
```

The index expression must have an integer type.

### 6.9 Error Propagation

The `?` operator propagates an error value out of the current function when applied to an error union expression.

If the expression evaluates to a success value, that value is unwrapped and used.

If the expression evaluates to an error value, the current function returns that error immediately.

### 6.10 Compile-Time Evaluation

Coda supports guaranteed compile-time evaluation through the `$` operator.

The `$` operator may be applied to both expressions and statements.

Applying `$` guarantees that the annotated code is evaluated during compilation.

If compile-time evaluation is not possible, the program is ill-formed and the compiler must emit an error.

#### Compile-Time Expressions

A compile-time expression is written by prefixing an expression with `$`.

Example:

```coda
string path = $strcat(#file(), "/config.json");
```

The value produced by a compile-time expression is embedded into the compiled program.

### Compile-Time Statements

A compile-time statement is written by prefixing a statement with `$`.

Example:

```coda
${
    println("Generating lookup table...");
    generate_lookup_table();
}
```

The statement is executed during compilation.

Any side effects produced by a compile-time statement occur during compilation and are not repeated at runtime.

#### Compile-Time Guarantees

The `$` operator is not a hint to the compiler.

It is a language guarantee.

A `$` expression or statement must execute directly at compile time.

If the compiler cannot satisfy this guarantee, compilation must fail.

The set of operations permitted during compile-time evaluation is implementation-defined and includes the compile-time intrinsics described in the Intrinsics section.

## 7. Statements

A statement performs an action.

Statements do not directly produce values.

Statements include:

* Block statements
* Variable declaration statements
* Conditional statements
* Loop statements
* Match statements
* Return statements
* Break and continue statements
* Defer statements
* Compile-time statements

### 7.1 Blocks

A block statement is a sequence of zero or more statements enclosed in braces.

Syntax:

```coda
{
    statements
}
```

A block introduces a new scope.

Names declared in a block are visible only within that block and its nested blocks.

Coda does not permit shadowing. A name may not be declared more than once in the same scope, and a declaration may not reuse a name already visible from an enclosing scope.

### 7.2 Variable Declaration Statements

A variable declaration statement introduces a local variable.

Syntax:

```coda
Type name = expression;
```

Example:

```coda
int count = 0;
```

A local variable declaration must be initialised unless the type system explicitly defines a default or implicit initialisation rule for that type.

Local variables may be mutable or immutable according to the mutability rules defined by the type system.

### 7.3 Conditional Statements

A conditional statement executes one of two branches depending on a boolean condition.

Syntax:

```coda
if (condition) {
    then_branch
} else {
    else_branch
}
```

The condition expression must have type `bool` or optional pointer.

The `else` branch is optional.

Example:

```coda
if (args.len != 2) {
    io::println("Missing required arguments!");
}
```

### 7.4 Loop Statements

#### While Loops

A `while` loop repeatedly executes its body while its condition remains true.

Syntax:

```coda
while (condition) {
    body
}
```

The condition expression must have type `bool` or optional pointer.

#### For Loops

A `for` loop is a C-style loop.

Syntax:

```coda
for (initialiser; condition; post) {
    body
}
```

The initialiser may be a variable declaration or an expression statement.

The condition expression must have type `bool` if present.

The post expression is evaluated after each iteration if present.

Example:

```coda
for (mut uint i = 0; i < args.len; i = i + 1) {
    io::println(args[i]);
}
```

### 7.5 Match Statements

A `match` statement selects a branch based on the runtime type or value of an expression.

Syntax:

```coda
match (expression) {
    pattern1 {
        body
    }
    pattern2 {
        body
    }
}
```

A match arm introduces a new scope for its body.

If the matched value is a sum type, the match may bind the contained value to a name.

Example:

```coda
match (setup_network()) {
    FileError err {
        io::printn($enumstr(err));
    }
    int val {
        io::printn("Flawless.");
    }
}
```

The exact exhaustiveness rules for match are defined by the type system and error handling rules.

### 7.6 Return Statements

A return statement exits the current function.

Syntax:

```coda
return expression;
```

If the function returns `none`, the expression may be omitted.

Example:

```coda
return;
```

### 7.7 Break and Continue Statements

A `break` statement exits the nearest enclosing loop.

A `continue` statement skips to the next iteration of the nearest enclosing loop.

Syntax:

```coda
break;
continue;
```

These statements are only valid within loop bodies.

### 7.8 Defer Statements

A `defer` statement schedules a statement or expression to run when execution leaves the current scope.

Syntax:

```coda
defer expression;
```

Example:

```coda
defer std::mem::default_heap.free(buffer);
```

Deferred actions run in reverse order of declaration when the current scope is exited, including via return, break, or error propagation.

### 7.9 Compile-Time Statements

A compile-time statement is a statement prefixed with `$`.

Syntax:

```coda
$ {
    statements
}
```

Example:

```coda
${
    io::println("Generating lookup table...");
    generate_lookup_table();
}
```

A compile-time statement is evaluated during compilation.

If compile-time evaluation is not possible, compilation must fail.

Any side effects produced by a compile-time statement occur during compilation and are not repeated at runtime.

Compile-time statements obey the same scoping rules as ordinary statements.

## 8. Generics

Generics allow declarations to operate on one or more types specified by the caller.

Generic declarations are instantiated for the concrete types used by the program.

Generic instantiation is performed at compile time.

### 8.1 Generic Parameters

A generic parameter list appears immediately after the declaration name.

Syntax:

```coda
fn none foo<T>() {

}
```

A generic parameter introduces a type that may be used anywhere within the declaration.

Example:

```coda
fn T identity<T>(T value) {
    return value;
}
```

The compiler creates a concrete instantiation of the declaration for each unique set of type arguments.

### 8.2 Explicit Type Arguments

Type arguments may be specified explicitly.

Syntax:

```coda
name<TypeArguments>(arguments);
```

Example:

```coda
int value = identity<int>(42);
```

If the compiler can determine the required type arguments from the function arguments, explicit type arguments may be omitted.

Example:

```coda
int value = identity(42);
```

which is equivalent to:

```coda
int value = identity<int>(42);
```

### 8.3 Generic Types

User-defined types may also be generic.

Example:

```coda
type Vec3<T> = struct {
    T x;
    T y;
    T z;
};
```

Each distinct set of type arguments produces a distinct concrete type.

Example:

```coda
Vec3<int>
Vec3<float>
```

are different types.

### 8.4 Constraints

A generic parameter may specify one or more constraints.

A constraint describes the operations that a type must support to satisfy the generic declaration.

Syntax:

```coda
T : constraint
```

Example:

```coda
fn none load<T: fn uint64 read(mut T *self, uint8[] buffer)>(mut T *source) {

}
```

The generic argument must satisfy every required constraint.

Failure to satisfy a constraint is a compile-time error.

### 8.5 Multiple Constraints

Multiple constraints for a single generic parameter are separated using semicolons.

Example:

```coda
fn none copy<
    T :
        fn uint64 read(T mut *self, uint8[] buf);
        fn none write(T mut *self, uint8[] buf)
>(
    T mut *src,
    T mut *dst
) {
}
```

The supplied type must provide every required member.

### 8.6 Independent Constraints

Different generic parameters may have different constraints.

Example:

```coda
fn none pipe<
    R : fn uint64 read(R mut *self, uint8[] buf),
    W : fn none write(W mut *self, uint8[] buf)
>(
    R mut *src,
    W mut *dst
) {
}
```

Each parameter is checked independently.

### 8.7 Constraint Resolution

Constraint satisfaction is structural.

A type satisfies a constraint if it provides every required declaration with a compatible type.

No explicit interface declaration or implementation is required.

Example:

```coda
fn uint64 File.read(mut File *self, uint8[] buf) {

}
```

A `File` value satisfies any generic constraint requiring a compatible `read` method.

### 8.8 Generic Methods

Methods may themselves be generic.

Example:

```coda
fn T Box.get<T>(Box<T> *self) {
    return self->value;
}
```

Generic parameters declared by a method are independent of those declared by its enclosing type.

### 8.9 Instantiation

Generic declarations are instantiated only when required.

A compiler should not generate code for unused generic instantiations.

Each unique combination of type arguments produces one concrete instantiation.

Equivalent instantiations may be shared by the implementation.

## 10. Attributes

Attributes provide additional information to the compiler about a declaration or type.

Attributes do not introduce new language constructs. Instead, they modify the behaviour or representation of an existing declaration.

An attribute begins with the `@` character followed immediately by an identifier.

Example:

```coda
@export
fn int main(string[] args) {
    return 0;
}
```

### 10.1 Attribute Placement

Attributes apply to the declaration or type definition immediately following them.

Examples:

```coda
@export
fn int add(int a, int b) {
    return a + b;
}

@packed
struct Vec3 {
    int x;
    int y;
    int z;
}
```

Multiple attributes may be applied to the same declaration.

Example:

```coda
@export
@extern
fn none puts(char *string);
```

The order of attributes has no semantic meaning unless explicitly specified by the attribute.

### 10.2 Attribute Arguments

An attribute may optionally accept arguments enclosed in parentheses.

Syntax:

```coda
@attribute(arguments)
```

The meaning of an attribute's arguments is defined by said attribute.

### 10.3 Unknown Attributes

Applying an unknown attribute is ill-formed.

Implementations must diagnose the use of attributes they do not recognise.

### 10.4 Built-in Attributes

The following attributes are defined by the language.

#### `@export`

The `@export` attribute makes a declaration visible outside its defining module.

Without `@export`, declarations are private to their module.

Example:

```coda
@export
fn int main(string[] args) {
    return 0;
}
```

#### `@extern`

The `@extern` attribute declares that a function or variable is defined outside the current compilation unit.

Extern declarations do not use Coda name mangling.

Example:

```coda
@extern
fn none puts(char *string);
```

An `@extern` function declaration may omit its body.

#### `@packed`

The `@packed` attribute requests that the implementation minimise padding within a structure.

Example:

```coda
@packed
struct Header {
    uint16 type;
    uint32 length;
}
```

A packed type may have stricter alignment requirements or reduced access performance depending on the target architecture.

The exact layout guarantees are defined by the Memory Model.

### 10.5 Future Attributes

Implementations may provide implementation-defined attributes.

Implementation-defined attributes should use a reserved namespace to avoid conflicts with future language-defined attributes.

## 11. Intrinsics

Intrinsics are compiler-provided operations.

Intrinsics are part of the language, but they are not ordinary functions.

An intrinsic is written using the `#` prefix.

Examples:

```coda
#file()
#line()
#typeid(value)
#sizeof(T)
```

### 11.1 Nature of Intrinsics

Intrinsics are resolved directly by the compiler.

They are not looked up through ordinary name resolution.

They do not have user-defined bodies.

They cannot be overridden by ordinary declarations.

They do not support operator overloading.

An intrinsic may correspond to:

* A compile-time value
* A code-generation primitive
* A type query
* A source-location query
* A compiler-recognised lowering rule

### 11.2 Compile-Time Availability

Some intrinsics are only valid during compile-time evaluation.

If a compile-time-only intrinsic is used outside a compile-time context, the program is ill-formed.

Examples of compile-time intrinsics include source-location and type-introspection intrinsics.

### 11.3 Type Introspection Intrinsics

Coda provides intrinsics for inspecting types at compile time.

Examples include:

* `#typeid(value)` — obtains the type identifier of a value or type
* `#typestr(value)` — obtains a string representation of a type
* `#enumstr(value)` — obtains the name of an enum variant as a string

The exact representation of type identifiers is defined in typeid.md

### 11.4 Source Information Intrinsics

Coda provides intrinsics for obtaining source information.

Examples include:

* `#file()` — the current source file
* `#line()` — the current line number
* `#column()` — the current column number
* `#module()` — the current module name

These intrinsics are useful for diagnostics, logging, and compile-time generation.

### 11.5 Layout Intrinsics

Coda provides intrinsics for querying type layout.

Examples include:

* `#sizeof(T)` — the size of `T` in bytes
* `#alignof(T)` — the alignment of `T` in bytes
* `#offsetof(T, member)` — the byte offset of a member within a type

Layout intrinsics reflect the layout rules of the target platform and the type system.

### 11.6 Restrictions

Intrinsics are not first-class values.

Their address may not be taken.

They may not be assigned to variables.

They may not be passed as ordinary function values unless the language explicitly defines a lowering for that use.

An intrinsic may only be used in forms recognised by the compiler.

If the compiler does not recognise a use of an intrinsic, the program is ill-formed.

## 12. Memory Model

The memory model describes how Coda programs represent, access, and retain storage.

The memory model defines observable behaviour. Implementations may use any internal representation, provided the observable rules of the language are preserved.

### 12.1 Objects and Storage

An object is a region of storage associated with a type.

Every object has an address.

Objects may exist in one of three storage durations:

* Static storage
* Automatic storage
* Dynamic storage

### 12.2 Static Storage

Static storage is storage that exists for the duration of program execution.

Global variables occupy static storage.

Data produced by compile-time evaluation may also be placed in static storage.

Static storage is created before program startup and released only when the program terminates.

### 12.3 Automatic Storage

Automatic storage is storage associated with block scope.

Local variables ordinarily occupy automatic storage.

Automatic storage is created when execution enters the declaring scope and is released when execution leaves that scope.

Deferred actions associated with the scope are executed before the storage is released.

### 12.4 Dynamic Storage

Dynamic storage is storage obtained at runtime through allocation facilities.

Dynamic storage remains valid until it is explicitly released by the program.

The exact allocation API is defined by the standard library, not by the language core.

### 12.5 Lifetimes

Every object has a lifetime.

An object's lifetime begins when its storage is made available for that object.

An object's lifetime ends when its storage is released or reused for another object.

Any pointer referring to an object becomes invalid once that object's lifetime has ended.

### 12.6 Pointers

A pointer stores the address of an object.

A non-optional pointer is guaranteed not to be `null` by the type system.

An optional pointer may contain `null`.

Dereferencing a null pointer or a pointer to an object whose lifetime has ended is invalid.

Pointer values may alias the same object.

The language does not impose uniqueness or borrow restrictions on ordinary pointers.

### 12.7 Mutability and Aliasing

Mutability is a property of the access path, not a guarantee of exclusivity.

A mutable access path permits modification of the referenced object through that path.

An immutable access path permits reading but not writing through that path.

Multiple mutable or immutable pointers may refer to the same object simultaneously, subject to the type rules governing the access paths themselves.

The `mut` qualifier determines whether a particular access path may be used to modify storage.

### 12.8 Arrays

Arrays own their data.

Array elements are stored contiguously in index order.

For a type `T[n]`, the array contains exactly `n` elements of type `T`.

Unless otherwise specified by an attribute such as `@packed`, elements are laid out without reordering.

### 12.9 Slices and Strings

A slice is a non-owning view over a contiguous sequence of elements.

A slice contains:

* A length
* A pointer to the first element

Slices do not own the storage they reference.

The referenced storage must remain valid for the lifetime of the slice value.

The `string` type is a slice of `char` values.

String literals may refer to static storage.

### 12.10 Structs

Struct fields are laid out in declaration order.

The implementation may insert padding between fields to satisfy alignment requirements.

The exact offsets of fields are determined by the type layout rules and the target platform.

### 12.11 Unions

All fields of a union occupy overlapping storage.

A union value is large enough to hold any of its fields.

The active field of a union is determined by the program's use of the value.

Reading a field that is not currently active is invalid unless the language or implementation explicitly defines a conversion or reinterpretation rule.

### 12.12 Sum Types

A sum type contains one active variant at a time.

A sum type stores:

* A tag identifying the active variant
* Storage sufficient for the largest variant

Nested sum types are flattened.

The order of variants in a sum type does not affect the meaning of the type.

The exact in-memory representation of the tag and payload is implementation-defined, provided the abstract behaviour of the type is preserved.

### 12.13 Packed Types

The `@packed` attribute requests that the implementation minimise padding.

A packed type may reduce or remove padding between fields.

The exact layout of a packed type is defined by the Memory Model and the target architecture.

Packed types may impose stricter access requirements or lower performance than unpacked types.

### 12.14 Global Initialisation

Global variables must be initialised.

A global variable may be initialised in one of two ways:

* By a compile-time value
* By a runtime start routine executed before `main`

Compile-time initialisation is performed during compilation and becomes part of the program image.

Runtime initialisation is performed before program entry and may depend on other global initialisation rules.

The order of runtime global initialisation follows module dependency order.

### 12.15 Invalid Access

The following are invalid:

* Dereferencing a null pointer
* Dereferencing a dangling pointer
* Accessing an object after its lifetime has ended
* Reading or writing outside the bounds of an array or slice
* Accessing a union field that is not active, unless explicitly permitted

If the compiler can prove such an access is invalid, it shall reject the program.

Otherwise, the resulting behaviour is undefined.

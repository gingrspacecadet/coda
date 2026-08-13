# Coda Language Specification - V2

## Table of Contents

1. Introduction
2. Lexical Structure
3. Grammar
4. Modules and Names
5. Declarations
6. Types
7. Expressions
8. Statements
9. Functions
10. Generics and Constraints
11. Memory and Resources
12. Pointers and Slices
13. Sum Types and Error Handling
14. Pattern Matching
15. Comptime
16. Reflection
17. Code Generation and AST Manipulation
18. Attributes
19. Evaluation and Initialization
20. Concurrency
21. ABI and FFI
22. Safety and Invalid Operations
23. Diagnostics
24. Implementation Requirements
25. Standard Library

## 1. Introduction

# Coda Language Specification

## 1. Introduction

Coda is a systems programming language designed around explicit
behaviour, predictable execution, and a small conceptual core.

Coda provides direct control over memory and machine-level resources
without requiring programmers to surrender the conveniences expected
from a modern language.

The language is designed to make important program properties explicit:

- mutability is explicit,
- memory management is explicit,
- pointer semantics are precisely defined,
- slices carry their bounds explicitly,
- errors and optional values are represented by sum types,
- compile-time execution is part of the language,
- reflection is available at both compile time and runtime, and
- unsafe operations are explicit.

The Coda language specification defines the syntax and semantics of
the language independently of any particular compiler implementation.

A conforming implementation may use any internal architecture or
code-generation strategy, provided that the observable behaviour of
well-defined Coda programs conforms to this specification.

### 1.1 Conventions

The terms **must**, **must not**, **should**, and **may** are used in their usual normative sense.

Unless otherwise specified, behaviour is defined by the language rather than by a particular target architecture.

Where behaviour is explicitly target-dependent, the implementation must document the applicable target-specific rules.

## 2. Lexical Structure

### 2.1 Source Encoding

Coda source files are encoded as UTF-8.

The lexical syntax of Coda is ASCII-based. Unicode characters may appear in character and string literals, but are not permitted in identifiers or keywords.

### 2.2 Whitespace

Whitespace separates tokens and has no semantic meaning.

The following characters are whitespace:

- Space (`U+0020`)
- Horizontal tab (`U+0009`)
- Line feed (`U+000A`)
- Carriage return (`U+000D`)

Whitespace may occur between any two tokens where the grammar permits token separation.

### 2.3 Comments

Coda supports line and block comments.

A line comment begins with `//` and extends to the end of the line.

A block comment begins with `/*` and ends with `*/`.

Block comments may be nested.

Comments are treated as whitespace.

### 2.4 Identifiers

An identifier begins with an ASCII letter or `_`, followed by zero or more ASCII letters, decimal digits, or `_`.

Identifiers are case-sensitive.

```coda
foo
Foo
_count
value2
```

are distinct identifiers.

Unicode characters are not permitted in identifiers.

### 2.5 Keywords

The following identifiers are reserved as keywords:

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
none
```

This list may be extended by future revisions of the language.

### 2.6 Attributes

An attribute begins with `@` followed by an identifier.

```coda
@export
@extern
@packed
```

An attribute may have zero or more comma-separated compile-time arguments:

```coda
@attribute
@attribute(value, expression)
```

Attributes provide metadata to the compiler. Attributes do not execute user-defined code.

The semantics of individual attributes are defined by the relevant portion of this specification.

### 2.7 Compiler Intrinsics

A compiler intrinsic begins with `#`.

Examples include:

```coda
#sizeof(T)
#type(...)
#fields(T)
```

Compiler intrinsics are operations or values understood by the compiler itself. They are not runtime library functions.

An intrinsic does not imply compile-time execution merely because it begins with `#`; its semantics are determined by the intrinsic.

### 2.8 Compile-Time Evaluation

The `$` prefix requests compile-time evaluation of the immediately following construct.

It may precede an expression or statement:

```coda
$x
$foo()
$if (condition) {

}
```

A function declaration may be explicitly declared as a compile-time function:

```coda
$fn int square(int x) {
    return x * x;
}
```

A braced compile-time block may also be written:

```coda
${
    ...
}
```

A `$` prefix modifies the construct that immediately follows it. It does not introduce a distinct runtime construct.

### 2.9 Code Quotation

`#{...}` introduces a quoted source-code fragment.

The contents are parsed using the normal Coda grammar.

```coda
#{
    fn int add(int a, int b) {
        return a + b;
    }
}
```

A code quotation produces a compile-time code value.

Code quotations do not execute the contained code.

### 2.10 Code Splicing

Within a code quotation, `#(expression)` denotes a splice.

```coda
#{
    fn #(name)() {
        return #(value);
    }
}
```

The parser represents a splice as a syntax node containing the expression. The expression is resolved and evaluated during compile-time expansion.

A splice is structural: it inserts a compiler-recognized code value into an AST position. It does not perform textual substitution.

The type of the resulting code value must be compatible with the syntactic position into which it is inserted.

### 2.11 Integer Literals

Integer literals are initially untyped.

Decimal literals:

```coda
0
42
1_000_000
```

Hexadecimal:

```coda
0xFF
0xDEAD_BEEF
```

Binary:

```coda
0b1010_0101
```

Octal:

```coda
0o755
```

Underscores may separate digits but may not occur at the beginning or end of a literal, immediately after a radix prefix, or consecutively.

Negative integer literals are formed with the unary `-` operator. `-42` is not itself an integer literal.

### 2.12 Floating-Point Literals

A floating-point literal contains either a decimal point or an exponent.

```coda
0.0
1.5
3.
1e6
6.022e23
```

Floating-point literals are initially untyped.

### 2.13 Character Literals

A character literal is enclosed in single quotes.

```coda
'a'
'Z'
'の'
'\n'
```

A character literal represents a Unicode scalar value.

A character literal is not intrinsically the `char` alias.

The value of a character literal may be assigned to an integer type if that integer type can represent the Unicode scalar value.

For example:

```coda
uint8  a = 'A';
uint16 b = 'の';
uint32 c = 'の';
```

is valid, while:

```coda
uint8 x = 'の';
```

is invalid because U+306E cannot be represented by uint8.

### 2.14 String Literals

A string literal is enclosed in double quotes.

```coda
"hello"
"hello\nworld"
"の"
```

A string literal represents the UTF-8 encoding of its contents.

The language does not require a null terminator.

The result is a byte slice and does not require the compiler to depend on a standard-library `string` declaration.

The standard library may provide:

```coda
type char = uint8;
type string = char[];
```

through the prelude.

### 2.15 Escape sequences

Character and string literals support at least:

```coda
\\
\"
\'
\n
\r
\t
\0
\xNN
```

where `NN` consists of two hexadecimal digits.

Unicode escape syntax is specified separately from byte escapes.

### 2.16 Boolean Literals

The boolean literals are:

```coda
true
false
```

### 2.17 Tokenisation

The lexer selects the longest valid token beginning at the current source position.

For example:

```coda
>>=
!&=
<<
::
```

must be recognised as their corresponding multi-character tokens rather than as sequences of shorter tokens.

The lexical grammar is independent of semantic analysis.

## 3. Grammar

The grammar in this specification uses EBNF notation.

The grammar describes syntax only. Semantic validity is specified separately.

### 3.1 Module

```ebnf
module =
    "module", path, ";",
    { declaration }
    ;
```

### 3.2 Declarations

```ebnf
declaration =
    { attribute },
    (
        include_decl
      | type_decl
      | function_decl
      | variable_decl
    )
    ;
```

A declaration may have zero or more attributes.

### 3.3 Include Declarations

```ebnf
include_decl =
    "include",
    path,
    [ "=", identifier ],
    ";"
    ;
```

### 3.4 Type Declarations

```ebnf
type_decl =
    "type",
    identifier,
    [ generic_parameters ],
    "=",
    type_definition,
    ";"
    ;
```

A type declaration may introduce:

```coda
type Int = int32;

type Vec = struct {
    int32 x;
    int32 y;
};

type Result = union {
    int32 value;
    Error error;
};
```

### 3.5 Type Definitions

```ebnf
type_definition =
      type
    | struct_definition
    | union_definition
    | enum_definition
    ;
```

### 3.6 Variables

```ebnf
variable_decl =
    type,
    identifier,
    [ "=", expression ],
    ";"
    ;
```

Variables are immutable by default. Mutability is expressed by the type/declaration syntax defined in the Types chapter.

### 3.7 Struct Definitions

```ebnf
struct_definition =
    "struct",
    "{",
    { field_decl },
    "}"
    ;

field_decl =
    type,
    identifier,
    ";"
    ;
```

### 3.8 Union Definitions

```ebnf
union_definition =
    "union",
    "{",
    { field_decl },
    "}"
    ;
```

### 3.9 Enum Definitions

```ebnf
enum_definition =
    "enum",
    [ ":", type ],
    "{",
    { enum_value },
    "}"
    ;

enum_value =
    identifier,
    [ "=", expression ],
    ","
    ;
```

The optional underlying type specifies the representation of the enum.

### 3.10 Types

```ebnf
type =
    type_base,
    { type_modifier }
    ;

type_base =
      path
    | generic_type
    | function_type
    ;

generic_type =
    path,
    "<",
    type_list,
    ">"
    ;

type_list =
    type,
    { ",", type }
    ;

function_type =
    "fn",
    type,
    "(",
    [ parameter_types ],
    ")"
    ;

parameter_types =
    type,
    { ",", type }
    ;

type_modifier =
      pointer_modifier
    | array_modifier
    ;

pointer_modifier =
    [ "mut" ],
    "*",
    [ "?" ]
    ;

array_modifier =
      "[", "]"
    | "[", integer_literal, "]"
    ;
```

T[] denotes a slice.

T[n] denotes a fixed-size array.

An optional pointer is written T*?.

General optional values are represented by sum types, not by ?.

For example:

```coda
(string | none)
```

is a sum type.

### 3.11 Paths

```ebnf
path =
    identifier,
    { "::", identifier }
    ;
```

Paths are used to refer to declarations across module boundaries and to qualify names.

## 4. Modules and Names

Every Coda source file belongs to exactly one module.

A module declaration establishes the namespace containing the declarations in the file.

```coda
module game::math;
```

The module path is independent of the filesystem representation used by an implementation.

### 4.1 Declaration Visibility

Declarations are private to their defining module by default.

The @export attribute makes a declaration visible to other modules.

```coda
@export
fn int add(int a, int b) {
    return a + b;
}
```

A declaration that is not exported cannot be referenced by another module through normal module lookup.

### 4.2 Includes

An include declaration makes another module available to the current module.

```coda
include std::io;
```

An alias may be provided:

```coda
include std::io : io;
```

The included module is then referred to through the alias:

```coda
io::println("hello");
```

The exact rules governing symbol lookup, re-export, cycles, and module initialization are defined later in this specification.

### 4.3 Name Resolution

Names are resolved according to lexical scope and module visibility.

The AST contains source names only.

Name resolution replaces source-level names with compiler-internal symbol identities during semantic analysis.

An AST identifier does not itself contain a resolved symbol.

### 4.4 No Shadowing

Coda does not permit declarations to shadow another declaration in the same applicable scope.

The complete rules for nested scopes and name conflicts are defined in the semantic-analysis portion of this specification.

## 5. Declarations

A declaration introduces a name into the current module or lexical scope.

Coda has four declaration forms:

```text
include declaration
type declaration
function declaration
variable declaration
```

Declarations are private by default. The `@export` attribute makes a declaration externally visible where permitted.

Attributes precede the declaration they annotate.

### 5.1 Include Declarations

An include declaration makes another module available to the current module.

```coda
include std::io;
include std::math : math;
```

An optional alias specifies the local name used to refer to the included module.

The included module remains subject to its own visibility rules.

An include declaration does not copy declarations from the target module into the current module.

### 5.2 Type Declarations

A type declaration binds a name to a type definition.

```coda
type UserId = uint64;
```

A type declaration may introduce a generic type:

```coda
type Pair<T> = struct {
    T first;
    T second;
};
```

The declared name is nominal. Two distinct type declarations do not become interchangeable merely because their underlying definitions are identical.

For example:

```coda
type UserId = uint64;
type ProductId = uint64;
```

`UserId` and `ProductId` are distinct nominal types.

Type aliases whose semantics differ from nominal type declarations are specified separately if provided by the language.

### 5.3 Struct Types

A struct definition introduces a product type containing a fixed sequence of named fields.

```coda
type Vec2 = struct {
    int32 x;
    int32 y;
};
```

Each field has a name and type.

Field names must be unique within a struct.

Fields may be accessed using member access:

```coda
v.x
v.y
```

The layout, alignment, padding, and ABI representation of structs are specified in the ABI section.

### 5.4 Union Types

A union definition introduces a type whose fields occupy overlapping storage.

```coda
type Value = union {
    int32 integer;
    float real;
};
```

All fields of a union begin at the same storage location.

Reading a field other than the field most recently written is subject to the rules defined by the memory and object model.

Union types are distinct from sum types. A union does not contain an automatically tracked discriminator.

### 5.5 Enum Types

An enum defines a finite set of named values.

```coda
type Colour = enum {
    Red,
    Green,
    Blue,
};
```

An enum may specify an underlying integer type:

```coda
type Colour = enum : uint8 {
    Red,
    Green,
    Blue,
};
```

The underlying type determines the representation and available value range.

Enum members may have explicitly specified values:

```coda
type Error = enum : uint8 {
    None = 0,
    NotFound = 1,
    PermissionDenied = 2,
};
```

The semantics of assigning arbitrary integer values to enum types are defined by the type-conversion rules.

### 5.6 Function Declarations

A function declaration introduces a callable function.

```coda
fn int add(int a, int b) {
    return a + b;
}
```

A function consists of:

* a name;
* zero or more generic parameters;
* zero or more parameters;
* a return type;
* a body.

The return type precedes the function name.

A function may have no body when its implementation is provided externally:

```coda
@extern
fn int puts(char[] text);
```

The rules governing declarations without bodies are specified by the ABI and FFI sections.

### 5.7 Methods

A function whose name is qualified by a type name is a method of that type.

```coda
fn int Vec2.length(Vec2 self) {
    ...
}
```

Methods are functions, not a separate declaration category.

A method is associated with its receiver's nominal type during semantic analysis.

Method lookup is therefore distinct from ordinary module-level function lookup.

### 5.8 Compile-Time Functions

A function may be declared for compile-time execution by preceding its declaration with `$`.

```coda
$fn int square(int x) {
    return x * x;
}
```

A compile-time function is callable during compilation.

A normal runtime function is not implicitly callable during compile time.

A compile-time function may call only operations permitted during compile-time evaluation.

The exact rules governing compile-time execution are specified in the Comptime section.

### 5.9 Variable Declarations

A variable declaration introduces a named object.

```coda
int x;
int y = 42;
```

Variables are immutable by default.

The mutability syntax is defined by the type system.

A variable with an initialiser is initialised before it may be read.

A variable without an initialiser has no usable value until it is explicitly initialised.

The rules for initialisation, definite assignment, and object lifetime are specified in the Memory and Execution sections.

### 5.10 Global Variables

Global variables are declarations occurring directly within a module.

A global variable must have a valid initialisation strategy.

A global may be initialised by a compile-time value:

```coda
int answer = $compute_answer();
```

or by the program's startup procedure where permitted.

The exact initialisation ordering of global variables is specified in the Initialisation section.

Global mutable state and concurrency are subject to the rules of the Concurrency section.

### 5.11 Duplicate Declarations

Two declarations in the same scope may not introduce the same name unless the language explicitly permits that declaration category to be repeated.

Declarations do not shadow declarations in an enclosing applicable scope.

The compiler must diagnose duplicate or conflicting declarations.

### 5.12 Declaration Attributes

Attributes may modify the interpretation of declarations.

For example:

```coda
@export
fn int add(int a, int b) {
    return a + b;
}
```

An attribute does not itself introduce a declaration or execute arbitrary user code.

An attribute may impose additional semantic requirements on the declaration to which it is attached.

The set of standard attributes and their semantics are defined separately.

## 6. Types

Every Coda expression has a type.

Types describe the representation and permitted operations of values.

Coda distinguishes between primitive types, nominal types, compound types, and function types.

### 6.1 Primitive Types

The language provides signed and unsigned integer types of fixed widths.

The required integer types are:

```text
int8
int16
int32
int64

uint8
uint16
uint32
uint64
```

The language also provides:

```text
bool
```

Additional implementation-defined integer types may be provided by a target or standard library where specified.

### 6.2 Signed Integer Types

A signed integer type contains values represented using two's-complement representation.

Signed arithmetic uses modulo (2^N) arithmetic for an `N`-bit type.

Overflow therefore wraps rather than producing undefined behaviour.

### 6.3 Unsigned Integer Types

Unsigned integer arithmetic is performed modulo (2^N), where `N` is the width of the integer type.

For example:

```coda
uint8 x = 255;
x = x + 1;
```

results in `x` having the value `0`.

### 6.4 Boolean Type

`bool` contains exactly two values:

```coda
true
false
```

Conditions may use values other than `bool` where permitted by the language's condition semantics.

### 6.5 Nominal Types

A type declaration introduces a nominal type identity.

Two separately declared nominal types remain distinct even when their underlying representations are identical.

```coda
type UserId = uint64;
type ProductId = uint64;
```

A value of `UserId` is not implicitly interchangeable with `ProductId`.

### 6.6 Pointer Types

A pointer type is written:

```coda
T*
```

A pointer is either a valid pointer to an object of the referenced type or, where explicitly permitted, a pointer whose validity cannot be established.

Pointers are non-nullable by default.

A nullable pointer is written:

```coda
T*?
```

A nullable pointer may contain `null`.

Pointer mutability is expressed as part of the pointer type.

The exact rules governing mutation through pointers are specified in the Memory and Pointer Model.

### 6.7 Slice Types

A slice type is written:

```coda
T[]
```

A slice represents a contiguous sequence of `T` values together with its length.

A slice is represented as a fat pointer containing a pointer to its first element and a length.

Slices do not own their storage by default.

A slice may be derived from an array or another slice.

Runtime indexing does not implicitly perform bounds checking.

Where the compiler can establish an out-of-bounds access at compile time, the program is rejected.

An out-of-bounds access that is not proven valid is subject to the memory-safety rules.

### 6.8 Fixed-Size Arrays

A fixed-size array type is written:

```coda
T[n]
```

where `n` is an integer constant.

The array contains exactly `n` contiguous elements of type `T`.

The size of a fixed array is part of its type.

```coda
uint32[4]
uint32[8]
```

are distinct types.

### 6.9 Function Types

A function type consists of a return type and zero or more parameter types.

```coda
fn int(int, int)
```

is the type of a function accepting two `int` values and returning `int`.

Functions themselves may be represented by function pointers where required by the ABI.

### 6.10 Sum Types

A sum type contains one value selected from a finite set of member types.

```coda
(int | string)
```

is a sum type containing either an `int` or a `string`.

Sum types may contain more than two alternatives:

```coda
(int | string | Error | none)
```

Each value of a sum type has exactly one active variant.

The compiler must ensure that operations which inspect a sum type account for the possible variants according to the pattern-matching rules.

### 6.11 The `none` Type

`none` is a language-defined type for use in places where nothing is expected.

Examples include:

```coda
(string | none)

fn none main();
```

A function returning `none` does not return a value.

`none` may also appear as a member of a sum type.

It is distinct from a null pointer and is not represented by pointer nullability.

### 6.12 Error Types

Error values are represented using sum types.

For example:

```coda
fn (int | FileError) open_file(string path) {
    ...
}
```

There is no separate error type category in the core type system.

Error propagation operates on the relevant sum-type representation.

### 6.13 Generic Types

Generic types are parameterized by one or more generic parameters.

```coda
type Pair<T> = struct {
    T first;
    T second;
};
```

A generic type is instantiated by supplying type arguments:

```coda
Pair<int32>
```

The rules governing generic constraints and instantiation are specified in the Generics section.

### 6.14 Type Mutability

Mutability is explicit.

An immutable value may not be modified.

A mutable value may be modified where the applicable type and expression rules permit it.

Mutability is part of the type system rather than a warning-level property.

### 6.15 Type Compatibility

Two values may be assigned to one another only when their types satisfy the assignment conversion rules.

Implicit conversions are permitted only where explicitly specified by the language.

In particular, signedness is not implicitly changed.

An implicit integer conversion may occur when the destination type can represent the source value without truncation, subject to the exact integer conversion rules.

Explicit casts may request conversions not permitted implicitly.

## 7. Expressions

Expressions compute values and may have compile-time or runtime evaluation.

Every expression has a type.

The syntax and semantics of operators, calls, conversions, indexing, member access, initialization, and compile-time expressions are defined in this section.

## 8. Statements



## 9. Functions



## 10. Generics and Constraints



## 11. Memory and Resources



## 12. Pointers and Slices



## 13. Sum Types and Error Handling



## 14. Pattern Matching



## 15. Comptime



## 16. Reflection



## 17. Code Generation and AST Manipulation



## 18. Attributes



## 19. Evaluation and Initialization



## 20. Concurrency



## 21. ABI and FFI



## 22. Safety and Invalid Operations



## 23. Diagnostics



## 24. Implementation Requirements



## 25. Standard Library



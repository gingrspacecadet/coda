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
1e6
100e-1
```

Hexadecimal:

```coda
0xFF
-0xDEAD_BEEF
0x1Ae2B
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

Exponential literals may be provided using the letter "e" or "E" followed by more digits. A negative sign is permitted after the "e" if it identifies an integer.

Negative integer literals are formed with the unary `-` operator. `-42` is not itself an integer literal.

### 2.12 Floating-Point Literals

A floating-point literal contains either a decimal point or an exponent.

```coda
0.0
1.5
3.
1e6
6.022E23
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
      | function_decl
      | variable_decl
    )
;
```

```ebnf
attribute = 
    "@", identifier,
    [
        "(",
        [
            expression,
            { ",", expression }
        ],
        ")"
    ]
;
```

A declaration may have zero or more attributes.

#### 3.2.1 Include Declarations

```ebnf
include_decl =
    "include",
    path,
    [ "=", identifier ],
    ";"
;
```

#### 3.2.2 Variable Declarations

```ebnf
variable_decl =
    type,
    identifier,
    [ "=", expression ],
    ";"
;
```

Variables are immutable by default. Mutability is expressed by the type/declaration syntax defined in the Types chapter.

#### 3.2.3 Type Declarations

```ebnf
type_decl =
    "type",
    identifier,
    [ generic_decl_item ],
    [ "=", type ],
    ";"
;
```

#### 3.2.4 Constraint Declarations

```ebnf
constraint_decl =
    "constraint",
    identifier,
    [ generic_decl_item ],
    [ "=", constraint ],
    ";"
;
```

#### 3.2.5 Function Declarations

```ebnf
function_decl =
    [ "$" ],
    "fn",
    type,
    identifier,
    [
        "<",
        generic_field,
        { ",", generic_field },
        ">"
    ],
    "(",
    [
        parameter_spec,
        { ",", parameter_spec }
    ],
    ")",
    "{",
    { statement },
    "}"
;

parameter_spec =
    type,
    identifier
;
```

#### 3.2.6 Generic Declaration Items

```ebnf
generic_decl_item = 
    "<",
    generic_field,
    { ",", generic_field },
    ">"
;

generic_field = 
    identifier,
    [
        ":",
        constraint,
    ]
;
```

### 3.3 Statements

```ebnf
statement =
    [ "$" ],
    (
        expression
      | declaration
      | block_stmt
      | if_stmt
      | for_stmt
      | while_stmt
      | break_stmt
      | continue_stmt
      | match_stmt
      | defer_stmt
    ),
    ";"
;
```

#### 3.3.1 Expression Statement

```ebnf
expr_stmt =
    expression
;
```

#### 3.3.2 Block Statement

```ebnf
block_stmt = 
    "{", statement, "}"
;
```

#### 3.3.3 If Statement

```ebnf
if_stmt = 
    "if",
    "(", expression, ")",
    statement,
    [
        "else",
        statement
    ]
;
```

#### 3.3.4 For Statement

```ebnf
for_stmt = 
    "for",
    "(",
    statement,
    expression, ";",
    expression,
    ")",
    statement
;
```

#### 3.3.5 While Statement

```ebnf
while_stmt = 
    "while",
    "(", expression, ")",
    statement
;
```

#### 3.3.6 Break Statement

```ebnf
break_stmt =
    "break",
    [ expression ]
;
```

#### 3.3.7 Continue Statement

```ebnf
continue_stmt =
    "break",
    [ expression ]
;
```

#### 3.3.8 Match Statement

```ebnf
match_stmt =
    "match",
    "(", expression, ")",
    "{",
    { match_part },
    "}",
;

match_part =
    type_expr,
    [ identifier ],
    block_stmt,
;
```

#### 3.3.9 Defer Statement

```ebnf
defer_stmt =
    "defer",
    [ expression ]
;
```

### 3.4 Expressions

```ebnf
expression = 
        literal
      | identifier
      | initializer
      | parenthesised_expr
      | unary_expr
      | binary_expr
      | assignment_expr
      | conditional_expr
      | call_expr
      | index_expr
      | member_expr
      | cast_expr
      | deref_expr
      | intrinsic_expr
      | bubble_expr
      | lambda_expr
      | code_expr
      | splice_expr
      | type
      | constraint
;
```

#### 3.4.1 Literals

```ebnf
literal = 
        null_literal
      | integer_literal
      | float_literal
      | boolean_literal
      | char_literal
      | string_literal
;

null_literal = ? defined in tokenisation ?;

integer_literal = ? defined in tokenisation ?;

float_literal = ? defined in tokenisation ?;

boolean_literal = ? defined in tokenisation ?;

char_literal = ? defined in tokenisation ?;

string_literal = ? defined in tokenisation ?;
```

#### 3.4.2 Initializers

```ebnf

initializer = 
    "{", { init_field }, "}"
;

init_field = 
    [
        ".",
        ( 
            identifier
            |
            "[", expression, "]"
        ),
        "="
    ],
    expression
;
```

#### 3.4.3 Parenthesised Expressions

```ebnf
parenthesised_expr =
    "(", expression, ")"
;
```

#### 3.4.4 Unary Expressions

```ebnf
unary_expr =
    unary_op,
    expression
;

unary_op =
        "+" | "-"
      | "~"
      | "!"
      | "&"
;
```

#### 3.4.5 Binary Expressions

```ebnf
binary_expr =
    expression,
    binary_op,
    expression
;

binary_op = 
        "+" | "-" | "*" | "/" | "%"
      | "&" | "|" | "^" | "~&" | "~|" | "<<" | ">>" | ">>>"
      | "&&" | "||" | "!&" | "!|"
      | "==" | "!=" | "<" | "<=" | ">" | ">="
;
```

#### 3.4.6 Assignment Expressions

```ebnf
assignment_expr =
    lvalue,
    assignment_op,
    expression
;

assignment_op =
        "="
      | "+=" | "-=" | "*=" | "/=" | "%="
      | "&=" | "|=" | "^=" | "~&=" | "~|=" | "<<=" | ">>=" | ">>>="
      | "&&=" | "||=" | "!&=" | "!|="
;

lvalue =
        identifier
      | member_expr
      | index_expr
      | deref_expr
;
```

#### 3.4.7 Conditional Expressions

```ebnf
conditional_expr =
    expression,
    "?",
    expression,
    ":",
    expression
;
```

#### 3.4.8 Call Expressions

```ebnf
call_expr = 
    expression,
    "(",
    [
        expression,
        { ",", expression }
    ],
    ")"
;
```

#### 3.4.9 Index Expressions

```ebnf
index_expr = 
    expression,
    "[",
    expression,
    "]"
;
```

#### 3.4.10 Member Expressions

```ebnf
member_expr =
    expression,
    ".",
    identifier
;
```

#### 3.4.11 Cast Expressions

```ebnf
cast_expr =
    "(",
    type,
    ")",
    expression,
;
```

#### 3.4.12 Dereferencing Expressions

```ebnf
deref_expr = 
    "*",
    expression
;
```

#### 3.4.13 Intrinsic Expressions

```ebnf
intrinsic_expr =
    "#",
    path,
    "(",
    [
        expression,
        { ",", expression }
    ],
    ")"
;
```

#### 3.4.14 Bubble Expressions

```ebnf
bubble_expr = 
    expression,
    "?"
;
```

#### 3.4.15 Lambda Expressions

```ebnf
lambda_expr =
    [ "$" ],
    "fn",
    type,
    [ generic_decl_item ],
    "(",
    [
        parameter_spec,
        { ",", parameter_spec }
    ],
    ")",
    "{",
    { statement },
    "}"
;
```

#### 3.4.16 Code Expressions

```ebnf
code_expr = 
    "#",
    "{",
    (* tbd *)
    "}"
;
```

#### 3.4.17 Splice Expressions

```ebnf
splice_expr = 
    "#",
    "(",
    expression,
    ")"
;
```

### 3.5 Types

```ebnf
type =
        type_literal
      | identifier
      | generic_type
      | intrinsic_type
      | mutable_type
      | pointer_type
      | array_type
      | function_type
      | struct_type
      | union_type
      | enum_type
      | generic_type
;
```

#### 3.5.1 Type Literals

```ebnf
type_literal =
        "none"
      | "bool"
      | integer_type_literal
      | "float16" | "float32" | "float64"
      | "any"
;

integer_type_literal =
      | "int8" | "int16" | "int32" | "int64"
      | "uint8" | "uint16" | "uint32" | "uint64"
;
```

#### 3.5.2 Generic Types

```ebnf
generic_type =
    type,
    "<",
    type,
    { ",", type },
    ">"
;
```

#### 3.5.3 Intrinsic Types

```ebnf
intrinsic_type =
    "#",
    path
;
```

#### 3.5.4 Mutable Types

```ebnf
mutable_type = 
    "mut",
    type
;
```

#### 3.5.5 Pointer Types

```ebnf
pointer_type = 
    type,
    [ "mut" ],
    "*",
    [ "?" ]
;
```

#### 3.5.6 Array Types

```ebnf
array_type = 
    type,
    [ "mut" ],
    "[",
    [ expression ],
    "]"
;
```

#### 3.5.7 Function Types

```ebnf
function_type =
    "fn",
    type,
    [ generic_decl_item ],
    "(",
    [
        parameter_spec,
        { ",", parameter_spec }
    ],
    ")",
;
```

#### 3.5.8 Structure Types

```ebnf
struct_type = 
    "struct",
    [ ":", constraint ],
    "{",
    { struct_type_field },
    "}"
;

struct_type_field = 
    type,
    identifier,
    ";"
;
```

#### 3.5.9 Union Types

```ebnf
union_type = 
    "union",
    "{",
    { union_type_field },
    "}"
;

union_type_field = 
    type,
    identifier
;
```

#### 3.5.10 Enumerated Types

```ebnf
enum_type = 
    "enum",
    [ ":", integer_type_literal ],
    "{",
    { enum_value },
    "}"
;

enum_value =
    identifier,
    [ "=", expression ]
;
```

### 3.6 Constraints

### 3.7 Paths

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

An expression produces a value.

Every expression has a type. An expression may also be designated for compile-time evaluation by the `$` prefix.

Expressions are evaluated according to the rules in this section and the applicable type and execution rules.

### 7.1 Literals

Literal expressions produce the value represented by the corresponding literal.

Integer and floating-point literals are initially untyped. Their type is determined by the surrounding context when required.

A literal whose value cannot be represented by the required type is invalid.

### 7.2 Identifiers

An identifier expression refers to a declaration visible from the current scope.

```coda
int x = 42;

fn none foo() {
    x;
}
```

The meaning of an identifier is determined by name resolution.

If no declaration is visible with the given name, compilation fails.

### 7.3 Paths

A path expression refers to a declaration through one or more namespace components.

```coda
math::sin(x);
```

Paths are resolved according to module and declaration visibility.

### 7.4 Function Calls

A function call consists of a callable expression followed by zero or more arguments.

```coda
foo(a, b);
```

Arguments are evaluated according to the evaluation-order rules of this specification.

The number and types of arguments must be compatible with the function's parameters.

A function requiring a mutable argument must receive an expression that is permitted to be modified according to the mutability rules.

### 7.5 Generic Function Calls

Generic arguments may be supplied explicitly:

```coda
foo<int32>(x);
```

Generic arguments precede the ordinary argument list.

A generic function may also permit the compiler to infer generic arguments from the call.

The rules governing inference and constraints are specified in the Generics section.

### 7.6 Member Access

A member expression accesses a field or method associated with an expression's type.

```coda
point.x;
point.length();
```

Field lookup and method lookup are distinct semantic operations.

A member name is resolved according to the nominal type of the receiver and the applicable visibility rules.

### 7.7 Indexing

An indexing expression accesses an element of an array or slice.

```coda
array[index]
```

For a slice, the index must be within the slice's bounds.

The compiler must reject an access whose invalidity can be established at compile time.

Runtime bounds checks are not implicitly inserted.

If the compiler cannot prove that a runtime index is valid, the program may require `unsafe` according to the memory-safety rules.

### 7.8 Address-of

The address-of operator produces a pointer to an addressable object.

```coda
int x;
int* p = &x;
```

The resulting pointer has the appropriate type and validity properties of the referenced object.

Taking the address of a temporary or otherwise non-addressable object is invalid in safe Coda.

Such an operation may be permitted in `unsafe` code where explicitly specified.

### 7.9 Dereference

The dereference operator accesses the object referred to by a pointer.

```coda
int* p = ...;
int x = *p;
```

Dereferencing a nullable pointer requires first establishing that the pointer is not `none`.

Dereferencing an invalid pointer is invalid.

### 7.10 Unary Operators

Coda provides unary operators including:

```text
-
+
!
~
*
&
```

The operand type must support the selected operation.

The unary `*` operator dereferences a pointer.

The unary `&` operator produces a pointer to an addressable object.

The semantics of the remaining unary operators are defined by the operand's type.

### 7.11 Binary Operators

Binary expressions have a left operand, an operator, and a right operand.

Coda provides arithmetic, comparison, logical, bitwise, and shift operators.

The language does not provide user-defined operator overloading.

Operators operate according to the types of their operands.

### 7.12 Logical Operators

`&&` and `||` are short-circuiting logical operators.

For:

```coda
a && b
```

`b` is evaluated only if `a` evaluates to a value that requires evaluating the right operand.

For:

```coda
a || b
```

`b` is evaluated only if `a` evaluates to a value that requires evaluating the right operand.

Conditions need not have type `bool`. The truth value of an expression is determined according to the language's condition rules.

### 7.13 Assignment

Assignment is an expression and produces the assigned value.

```coda
x = 42;
```

is therefore both a statement and an expression.

The left operand must designate a mutable object.

The right operand must be assignable to the left operand's type.

Assignment is evaluated from right to left where nested assignment expressions require an order.

### 7.14 Compound Assignment

Coda provides compound assignment operators.

A compound assignment is equivalent to applying the corresponding operation to the current value and assigning the result back to the left operand.

For example:

```coda
x += y;
```

is equivalent in effect to:

```coda
x = x + y;
```

The left operand is evaluated only once.

The complete set of compound operators is specified by the operator table.

Compound operators which combine existing operations are defined in terms of those operations rather than through operator overloading.

### 7.15 Increment and Decrement

If supported, increment and decrement operations are defined in terms of mutation of an integer or pointer value.

Their exact syntax and value semantics are specified by the operator table.

### 7.16 Cast Expressions

An explicit cast converts an expression to a specified type.

```coda
int32 x = (int32)value;
```

A cast does not imply that the conversion is safe.

Conversions which are not permitted implicitly may be requested explicitly where permitted by the language.

Conversions involving pointers, integers, signedness changes, and representation changes are subject to the memory and type-conversion rules.

### 7.17 Initializer Expressions

An initializer expression constructs a value from a sequence of field or element initializers.

```coda
Vec2 v = {
    .x = 10,
    .y = 20,
};
```

The fields or elements supplied by an initializer must correspond to the constructed type.

The compiler rejects duplicate, nonexistent, or otherwise invalid initializers.

### 7.18 Conditional Expressions

A conditional expression evaluates one of multiple expressions based on a condition where such a form is provided by the grammar.

The condition is evaluated first.

Only the selected branch is evaluated.

### 7.19 Error Propagation

The `?` operator propagates an error value out of the current function.

When applied to a sum-type expression representing success or failure:

* a success value is unwrapped and produced as the result of the expression;
* a failure value causes the current function to return that failure immediately.

The expression must have a type for which propagation is defined.

For example:

```coda
fn (int | FileError) read_file() {
    File file = open()?;
    return file.read()?;
}
```

The `?` operator does not represent nullable-pointer propagation. Nullable pointers use `T*?`, while optional values are represented using sum types.

### 7.20 Compile-Time Expressions

An expression preceded by `$` is evaluated during compilation.

```coda
int x = $square(10);
```

The expression must be valid for compile-time evaluation.

A compile-time expression may produce an ordinary Coda value which is embedded into the resulting program.

A compile-time expression may also produce a compiler-defined value such as a code object.

### 7.21 Compiler Intrinsic Expressions

Expressions beginning with `#` invoke compiler-defined operations or refer to compiler-defined compile-time values.

For example:

```coda
#sizeof(int)
```

and:

```coda
#fields(T)
```

are compiler intrinsic expressions.

The set and semantics of compiler intrinsics are defined by this specification and its compiler-facing extensions where applicable.

### 7.22 Code Splices

Inside a code quotation, `#(expression)` denotes a splice.

The expression is evaluated during compile-time expansion.

The resulting value must be a code value compatible with the syntactic position containing the splice.

For example:

```coda
$fn #code::ident uppercase(#code::ident id) {
    id.name = $string::upper(id.name);
    return id;
}
```

may be used to transform an identifier before the generated code is inserted.

Splicing does not perform textual substitution.

### 7.23 Lambda Expressions

A lambda expression creates an anonymous function.

```coda
fn int(int x) {
    return x + 1;
}
```

A lambda has:

* zero or more parameters;
* a return type;
* a body.

A lambda may capture values according to the capture and lifetime rules specified by the language.

### 7.24 Evaluation Order

Coda specifies evaluation order where observable effects could otherwise differ between implementations.

Operands of operators and arguments of function calls must be evaluated in the order specified by the relevant expression rule.

The implementation must not reorder evaluations in a manner observable through defined Coda behaviour.

The exact ordering rules are specified by the operator and call semantics.

---

# 8. Statements

A statement performs an action or controls execution.

A statement may be preceded by `$`, in which case it is executed during compile time rather than runtime.

## 8.1 Expression Statements

An expression followed by `;` forms an expression statement.

```coda
foo();
x = 42;
```

The resulting value is discarded unless the expression itself has relevant side effects.

## 8.2 Blocks

A block contains zero or more statements:

```coda
{
    int x = 1;
    foo(x);
}
```

A block introduces a lexical scope.

Names declared within a block are not visible outside it.

## 8.3 Return Statements

A return statement terminates execution of the current function.

```coda
return;
```

or:

```coda
return value;
```

A returned value must be compatible with the function's return type.

A function returning `none` may return without a value.

A `return` statement in a compile-time function terminates that compile-time invocation.

## 8.4 If Statements

An if statement conditionally executes one of two branches.

```coda
if (condition) {
    foo();
} else {
    bar();
}
```

The condition is evaluated before either branch.

Only the selected branch is executed.

Conditions need not have type `bool`.

## 8.5 While Statements

A while statement repeatedly executes its body while its condition is true.

```coda
while (condition) {
    step();
}
```

The condition is evaluated before each iteration.

## 8.6 For Statements

A for statement iterates according to the form defined by the Coda grammar.

The loop variable, iteration expression, and body each have their own applicable lexical scope.

The exact forms of `for` iteration are specified in the grammar and iteration semantics.

## 8.7 Match Statements

A match statement selects a branch based on the value of an expression.

```coda
match (value) {
    ...
}
```

Each match arm contains a pattern and an associated body.

The compiler verifies that a match is exhaustive where exhaustiveness can be determined from the matched type.

An incomplete match is rejected unless an explicit catch-all pattern is present.

## 8.8 Break Statements

`break` terminates the nearest enclosing loop.

```coda
break;
```

A `break` may not appear outside a loop.

## 8.9 Continue Statements

`continue` skips the remainder of the current iteration of the nearest enclosing loop.

```coda
continue;
```

A `continue` may not appear outside a loop.

## 8.10 Defer Statements

A defer statement schedules a statement for execution when the current scope is exited.

```coda
defer close(file);
```

The deferred statement executes when control leaves the scope containing the `defer`, regardless of whether the scope is exited normally or by `return`, `break`, `continue`, or another control-flow operation covered by the defer semantics.

A deferred statement executes according to the lexical order of registered defers.

Multiple deferred statements execute in reverse registration order.

`defer` is a source-level lifetime/control-flow construct. It does not survive as a distinct construct into later compiler representations.

## 8.11 Variable Declaration Statements

A variable declaration may appear as a statement within a block.

```coda
{
    int x = 42;
    foo(x);
}
```

The declaration introduces a name into the current lexical scope.

## 8.12 Compile-Time Statements

A statement preceded by `$` executes during compilation.

```coda
$foo();

$if (condition) {
    ...
}
```

The syntax and semantic validity of the underlying statement are the same as for its runtime form unless otherwise specified.

The resulting effects occur during compilation rather than runtime.

## 8.13 Compile-Time Blocks

A compile-time block executes a sequence of statements during compilation:

```coda
${
    int x = compute();
    println(x);
}
```

A compile-time block introduces a compile-time execution context.

Values created solely within a compile-time block cease to exist when the compile-time execution completes unless their results are explicitly materialized into the program.

---

# 9. Functions

## 9.1 Function Parameters

A function parameter has a type and a name.

```coda
fn none foo(int x, string y) {
    ...
}
```

Parameters are immutable unless their declared type permits mutation.

A parameter is initialized when control enters the function.

## 9.2 Function Return Types

Every function has an explicit return type.

```coda
fn int add(int a, int b) {
    return a + b;
}
```

A function returning `none` does not produce a value:

```coda
fn none log(string message) {
    ...
}
```

## 9.3 Function Bodies

A function with a body executes its statements when called.

```coda
fn int square(int x) {
    return x * x;
}
```

A function without a body must satisfy the requirements of the applicable external declaration mechanism.

## 9.4 Methods

Methods are associated with nominal types.

```coda
fn int Vec2.length(Vec2 self) {
    ...
}
```

A method may be called using member syntax:

```coda
v.length();
```

or using the corresponding callable representation where permitted.

Method lookup is based on the receiver's nominal type.

## 9.5 Compile-Time Functions

A compile-time function is declared with `$fn`.

```coda
$fn int square(int x) {
    return x * x;
}
```

It may be invoked from compile-time contexts.

A compile-time function may return an ordinary value:

```coda
int x = $square(4);
```

or a compiler-defined compile-time object such as a code object:

```coda
#{generate_function();}
```

A returned code object is not automatically inserted merely because its type is a code type. Code insertion occurs when the expression is used in a code-expansion context such as `#{...}`.

## 9.6 Function Calls and Side Effects

Calling a runtime function executes its body at runtime.

Calling a compile-time function from a compile-time context executes its body during compilation.

The same function definition is not simultaneously a runtime function and compile-time function unless explicitly declared as such by the language.

## 9.7 Function Pointers

A function may be converted to a function pointer where the target representation and calling convention permit it.

Function pointers may be stored, passed to functions, and called according to the function type.

The ABI section defines representation and calling convention requirements.

## 9.8 No Operator Overloading

Operators cannot be overloaded by user-defined functions.

The meaning of each operator is determined by the operand types and the built-in operator rules.

Compiler intrinsics may expose additional low-level operations without constituting operator overloading.

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



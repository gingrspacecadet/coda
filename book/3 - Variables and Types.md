# Chapter 3 - Variables and Types

Programs need somewhere to keep their data.

Coda provides variables for storing values, and a static type system for describing what those values are and how they may be used.

Unlike dynamically typed languages, Coda knows the type of every expression while compiling the program. This lets the compiler catch many mistakes before the program ever runs.

## Variables

A variable gives a value a name.

The simplest declaration consists of a type followed by a name:

```coda
int32 age = 21;
```

Here, `int32` is the type and `age` is the variable's name. `21` is the value initially stored in it.

Variables can be used in expressions just like other values:

```coda
int32 age = 21;
int32 next_age = age + 1;
```

You will learn more about expressions in the next chapter.

### Mutability

Values are immutable by default in Coda.

For example:

```coda
int32 count = 5;

count = 6;
```

The assignment is invalid because `count` was not declared as mutable.

To allow a variable to be changed, use `mut`:

```coda
mut int32 count = 5;

count = 6;
```

Now the assignment is valid.

This distinction is useful when reading code. A variable declared without `mut` cannot be changed after it is created, while a mutable variable may change over time.

Mutability is explicit in the source rather than being inferred from how a variable happens to be used.

## Types

A type describes what kind of value something is and what operations may be performed on it.

For example, an `int32` contains a signed 32-bit integer, while a `bool` contains a boolean value.

Coda is statically typed. The compiler checks the types of expressions and assignments while compiling the program.

For example:

```coda
int32 value = 42;
uint64 total = value;
```

This is not automatically valid simply because both values are integers. The language has specific rules governing which conversions are allowed and when a conversion must be written explicitly.

The type system is therefore part of how Coda communicates intent. A type does not merely describe how many bytes a value occupies; it also describes what that value means and which operations are valid.

## Integer Types

Coda provides fixed-width integer types.

Signed integers are:

```text
int8
int16
int32
int64
```

Unsigned integers are:

```text
uint8
uint16
uint32
uint64
```

The number in the name is the width of the integer in bits.

For example, `uint32` is always a 32-bit unsigned integer, regardless of the target architecture.

This makes the size of an integer part of its type rather than something that changes between platforms.

### Signed and Unsigned Integers

Signed integer types can represent negative and positive values.

```coda
int32 temperature = -10;
```

Unsigned integer types represent non-negative values:

```coda
uint32 count = 10;
```

Coda does not silently treat signed and unsigned integers as interchangeable. This helps prevent mistakes where a value is accidentally interpreted using the wrong representation.

Integer arithmetic uses fixed-width wrapping semantics. For example, incrementing the largest representable `uint8` value wraps back to zero:

```coda
uint8 value = 255;
value += 1;
```

After the addition, `value` is `0`.

## Boolean Values

The `bool` type represents a logical value.

There are two boolean literals:

```coda
true
false
```

For example:

```coda
bool enabled = true;
bool finished = false;
```

Boolean values are commonly used by control-flow constructs and logical expressions.

You will learn more about those in the next two chapters.

## None

Coda provides the `none` type for functions that do not return a value:

```coda
fn none print_message() {
    ...
}
```

`none` is also available as a type-level value representing the absence of a normal value. This becomes useful when constructing sum types later in the book.

For now, the most important use is simply that a function returning `none` does not produce a result for its caller.

## Characters

Character literals are written using single quotation marks:

```coda
'A'
'!'
'の'
```

A character literal represents a Unicode scalar value.

It is therefore not restricted to a single byte. For example, `'A'` and `'の'` are both valid character literals, even though their UTF-8 encodings have different lengths.

Character literals can be used with integer types that can represent their value:

```coda
uint8  ascii = 'A';
uint16 japanese = 'の';
uint32 unicode = 'の';
```

The type of the destination determines whether the value is representable.

This is an important distinction from languages where `char` necessarily means one byte. In Coda, the commonly used `char` name is instead provided by the standard library as an alias for `uint8`.

## Strings

String literals are written using double quotation marks:

```coda
"Hello, world!"
```

Coda represents string data as UTF-8 bytes.

A string literal is a sequence of those bytes and does not require a trailing NUL byte. This makes Coda strings suitable for APIs where the length is known separately from the data.

The standard library can provide convenient aliases for this representation. For example:

```coda
type char = uint8;
type string = char[];
```

Here, `string` is simply a convenient name for a slice of bytes.

You will learn more about arrays and slices later in the book.

## Literals

A literal is a value written directly into source code.

Some examples are:

```coda
42
true
'A'
"Hello!"
```

Literals are expressions, so they can be used anywhere an expression is expected.

### Integer Literals

Integer literals can be written directly as numbers:

```coda
42
1000
```

They can also use other integer literal forms where appropriate, such as hexadecimal notation:

```coda
0xff
```

Integer literals are initially untyped. Their type is determined by the context in which they are used.

For example:

```coda
int32 a = 42;
uint64 b = 42;
```

The same source literal can therefore represent an `int32` in one context and a `uint64` in another, provided the value can be represented by the chosen type.

This is particularly useful for constants because you do not need to write a different literal syntax for every integer type.

### Floating-Point Literals

Coda also supports floating-point values.

Floating-point literals are written using a decimal point when necessary:

```coda
3.14
5e-1
```

Like integer literals, floating-point literals can be given a type by their surrounding context.

Floating-point arithmetic and the available floating-point types will be covered alongside the other numeric operations later.

### Boolean Literals

Boolean values have two literals:

```coda
true
false
```

They are already values of type `bool`.

### Character and String Literals

Character and string literals have the forms introduced above:

```coda
'A'
"Hello!"
```

Their representations are different.

A character literal represents one Unicode scalar value, while a string literal represents a sequence of UTF-8 bytes.

## User-Defined Types

Coda does not limit you to the built-in types.

You can define your own named types using `type`.

For example:

```coda
type UserId = uint64;
type ProductId = uint64;
```

These are distinct types even though they use the same underlying representation.

A `UserId` is not automatically a `ProductId`, and vice versa.

This is called nominal typing; the identity of the type comes from the type declaration itself, rather than only from its underlying structure.

User-defined types become much more powerful when combined with structures, enums, unions, and generics. Those features will be introduced later in the book.

## Putting It Together

Here is a small program using several of the types introduced in this chapter:

```coda
include std::debug : dbg;

type UserId = uint64;

@entry
fn none main() {
    UserId user = 42;
    mut int32 age = 24;
    bool active = true;
    char initial = 'A';
    string greeting = "Hello, Coda!";

    dbg::println(user);
    dbg::println(age);
    dbg::println(active);
    dbg::println(initial);
    dbg::println(greeting);

    age += 1;
}
```

Most of the values in this program are immutable. Only `age` is declared with `mut`, so it is the only variable that can be changed.

## Experimenting

Try changing the types of the variables in the example.

Make `age` a `uint32`.

Try assigning an integer to `greeting`.

Try removing `mut` from `age` and compiling the program.

Try assigning `'の'` to a `uint8` and see what the compiler reports.

You can also experiment with integer literals and different numeric types. In particular, try using the same literal in variables with different types.

These small experiments are a good way to become familiar with Coda's type system.

## Summary

Variables give values names, while types describe what those values are and how they can be used. Coda's variables are immutable by default, with `mut` making mutation explicit.

You have now seen Coda's basic built-in types, integer widths, boolean values, character and string literals, and the foundations of nominal user-defined types.

In the next chapter, you will learn how values are combined using expressions and operators.

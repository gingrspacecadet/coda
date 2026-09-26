# Chapter 1 - Welcome to Coda

Introduction


## Why Coda?

Every language is a collection of trade-offs.

Some languages prioritise developer productivity over performance.
Others prioritise safety, or portability, or backwards compatibility.

Coda was designed around a different question.

> *How can a systems programming language remain simple whilst still providing modern features?*

Coda aims to be explicit yet succinct, making it practical to work comfortably at both high and low levels of abstraction.


## The Philosophy of Coda

Every feature in Coda exists because it supports one or more core principles.

### Explicitness

Operations should be visible in the source code. User-defined behaviour should never happen implicitly.

### Simplicity

Wherever possible, Coda prefers one powerful mechanism over several specialised ones.

For example, Coda does not have separate language features for optional values and result types. Instead, it provides inline sum types that can represent these concepts.

The language grows by composing existing ideas, rather than introducing new ones.

### Predictability

Reading code should be enough to understand what the program does and where its costs come from.

Coda deliberately avoids features such as implicit constructors, operator overloading, and the like, because they introduce behaviour that is not immediately visible in the source code.

### Compile-Time Programming

Many problems are easier to solve during compilation than at runtime.

Coda provides compile-time evaluation and reflection as first-class language features.

Using Coda itself for compile-time programming means that metaprograms can use the same language features as ordinary Coda programs, rather than requiring a separate preprocessor or scripting language.

### Zero-Cost Abstractions

High-level code does not imply slower code.

If an abstraction can be implemented without runtime overhead, it should be.

If runtime cost exists, it should be explicit.

### Libraries Before Language

Not every useful feature belongs in the language itself.

Functionality should live in libraries whenever the language can express it cleanly.

Keeping the language small makes it easier to learn, easier to implement, and easier to reason about.

## What Coda Is Not

Coda makes deliberate trade-offs. Understanding what the language does not provide is useful for understanding what it does provide.

### Not Object-Oriented

- No classes
- No inheritance
- No implicit virtual dispatch
- Types contain data; methods are functions

Instead, Coda provides a rich generic system, and special-syntax type methods that are just ordinary functions.

### Not Automatically Garbage-Collected

Coda does not impose a garbage collector on every program.

Memory is managed through explicit allocators, and an allocator can implement whatever strategy is appropriate for the program, including tracing garbage collection.

This means a Coda program can use:

- explicit allocation and freeing,
- arenas,
- pools,
- reference-counting allocators, or
- a tracing garbage collector.

The choice belongs to the program and its libraries rather than the language itself.

### Not Exception-Based

- No exceptions
- Errors are ordinary values
- Sum types can represent success/failure
- `return` makes control flow explicit

### Not a Separate Runtime

- No mandatory VM or managed runtime
- Native executables do not require a mandatory Coda runtime
- Some libraries may of course provide runtime services

### Not a Single Way of Programming

- Coda does not prescribe one abstraction level
- Low-level code, high-level libraries, generic code, compile-time code, and different allocation strategies can coexist
- The language provides mechanisms; the programmer chooses how to compose them

## A First Look

### The Program

```coda
include std::debug : dbg;

@entry
fn none main() {
    dbg::println("Hello, world!");
}
```

### Modules

Modules are how you organise your code. 

A "module" is just a named collection of types and functions.

The way you can use a module's contents is with `include`.

Above, you can see that we include the `std::debug` module, and give it the alias `dbg` through the `:`.

The alias means we can access the module as if it were named `dbg`.

Without the alias, we'd have to type `std::debug` every time instead.

### Functions

A function is a chunk of code. 

Functions have names, which describe what they do. By convention, the start of your program is called `main`, but that does not need to be so.

Here, we use the attribute `@entry` to denote the start of our program. Attributes will be explained more later.

### Types and Values

Types describe what kind of value something is and what operations may be performed on it. Coda has built-in types for things such as integers and booleans, as well as mechanisms for defining your own types.

Values are instances of those types. In the example above, `"Hello, world!"` is a string value passed to `println`.

You will learn about Coda's type system in much more detail later. For now, the important thing to understand is that Coda is statically typed: the compiler knows the type of every expression when it compiles the program.

### The Standard Library

The Standard Library ("stdlib" for short) is a large collection of implementations of common operations. The stdlib has code for networking, files, I/O, and much more.

### What Comes Next

So far, you have seen the ideas behind Coda and a small example of a complete program. The next chapters will build that program from the ground up.

You will start with variables, types, and expressions, then learn how to control program flow and organise code with functions. From there, the language will expand into arrays, user-defined types, modules, pointers, resources, generics, and compile-time programming.

Each feature will be introduced when you need it, with larger examples appearing as you progress.

## Summary

Coda is a systems programming language designed around:

- explicit behaviour,
- a small and composable language core,
- predictable costs,
- compile-time programming,
- low-overhead abstractions, and
- libraries rather than language features where practical.

Coda is designed to give you control without making that control unecessarily difficult to use. Its language is small and composable, its behaviour explicitm and its abstractions are intended to remain predictable. 

With those principles in mind, we can now being learning the language itself.
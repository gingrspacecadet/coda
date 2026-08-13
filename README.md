# Coda

Coda is a systems programming language exploring a simpler approach to low-level programming: explicit resources, predictable behaviour, and a small language core.

It aims to provide:

* C-like control and performance
* immutable-by-default semantics
* simple, explicit memory management
* fast compilation
* a complete toolchain written from scratch

## Hello World

```coda
module main;

include std::debug = dbg;

@export
fn int main() {
    dbg::println("Hello, world!");
    return 0;
}
```

Compile and run:

```bash
coda --run hello.coda
```

## Why Coda?

Coda is designed for programmers who want low-level control without the complexity that has accumulated around existing systems languages.

Unlike C, Coda provides stronger compile-time guarantees and safer defaults while keeping explicit control over memory, data layout, and performance.

Unlike higher-level languages, Coda avoids hidden allocations, runtime dependencies, and implicit behaviour.

Key design choices include:

- Immutable-by-default variables to prevent accidental mutation
- Explicit mutability through `mut`
- No warnings, only errors
- A simple compilation model with no dependency on large compiler frameworks
- Direct control over memory and data representation
- A complete toolchain designed as one coherent system

## Current status

Coda is under active development. The compiler pipeline currently includes:

* Lexer
* Parser
* Semantic analysis
* HIR lowering
* MIR generation
* x86-64 code generation

The language is not yet production-ready, but the compiler can already compile real programs.

## Design goals

Coda is built around a small set of principles:

* No hidden behaviour: allocations, mutations, and conversions should be explicit.
* Safe defaults: immutable-by-default and strict compile-time checking.
* Simple tooling: the compiler should be easy to understand and use.
* Built from first principles: avoid dependence on large compiler frameworks.

## Try it

Clone the repository:

```bash
git clone https://github.com/gingrspacecadet/coda
cd coda
. ./env
make
```

Run an example:

```bash
./coda --run examples/helloworld.coda
```

## Roadmap

Planned work includes:

* typeid system
* include caching
* expanded standard library
* target-independent LIR register allocation
* package management
* improved diagnostics

## Contributing

Coda is an experimental language project. Feedback, bug reports, and contributions are welcome.

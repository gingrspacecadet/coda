# Chapter 2 - Your First Program

This chapter will get Coda running on your machine and walk you through compiling your first program yourself.

## Installing Coda

<!-- Not finalised -->

To install Coda, download it from the github repository, or through the AUR.

To verify that it installed correctly, run:

```sh
codac --version
```

## Creating a Program

Coda files use the extension `.coda`. 

For your first project, create `main.coda` with these contents:

```coda
include std::debug : dbg;

@entry
fn none main() {
    dbg::println("Hello, world!");
}
```

## Running the Program

For basic programs, there is a simple way of executing them. 

Codac comes with the `--run` flag. What this does is automatically compile, link, and run the specified file.

Try it yourself:

```sh
codac --run main.coda
```

You should see `Hello, world!` printed to your terminal.

## Experimenting

Try changing the message to print different things. What about your name?

Maybe make it print multiple times!

What if you try and print something that isn't a string? Give it a go and see what happens! You should get a compiler error that explains what's wrong.

## Reading Errors

Here is an example compiler error:

```
error[E2004]: Type mismatch: expected 'uint8[]', found 'uint32'.
 --> e.coda:2:25
  |
2 |     mut uint8[] value = 0;
  |                         ^ 
  |
```

This shows you the code, `E2004`, which can be looked up on the website and in your man pages for more detail. Then, it explains what's wrong. As you can see, Coda does not allow you to assign `uint32` to `uint8[]`. Simple!

Compiler errors are expected during development. Especially when you're new; they should help guide you and teach you how to write correct code.

## Summary

You have now installed Coda, written your first program, compiled it, and ran it on your machine. You have also seen how Coda's compiler reports errors and how those diagnostics can help you find and fix mistakes.

In the next chapter, we'll start learning the language itself, beginning with variables and types.
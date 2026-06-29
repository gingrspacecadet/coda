# Coda package and build model

## Core principle

A package is just another Coda project.

There is no separate package format, no manifest file, and no language-level distinction between an application project and a library package. A project is simply a directory containing one or more Coda source files, one of which may serve as the build entry point by convention only.

`build.coda` is not special to the language. It is only a convention.

## Responsibilities

`core::pkg` is only for acquisition.

It can:

* clone repositories
* checkout commits, tags, or branches
* download archives
* extract archives
* copy or link local directories

It cannot:

* resolve package graphs
* infer dependencies
* execute foreign build scripts
* decide how a project should be built

`core::build` is only for build composition.

It can:

* define targets
* load another project’s build script
* compile sources
* link outputs
* compose dependency graphs from loaded targets

## Project layout

A typical project looks like this:

```text
project/
    build.coda
    src/
    include/
```

A vendored dependency looks the same:

```text
vendor/
    raylib/
        build.coda
        src/
        include/
```

There is no special package metadata file.

## Build scripts

A build script is ordinary Coda code.

It is the single source of truth for how a project builds, what it exports, and what it depends on.

A library exports one or more targets:

```coda
module build_script;

include core::build = build;
include std::fs = fs;

@export
build::Target[] targets = {
    build::library({
        .name = "raylib",
        .sources = fs::glob("src/*.c"),
        .include_dirs = {"include"},
        .defines = {"PLATFORM_DESKTOP"},
        .links = {"GL", "m", "pthread"}
    })
};
```

An application exports an executable target:

```coda
module build_script;

include core::build = build;
include std::fs = fs;

@export
build::Target[] targets = {
    build::executable({
        .name = "game",
        .sources = fs::glob("src/*.c"),
        .deps = {
            build::load("vendor/raylib/build.coda")
        }
    })
};
```

## Loading build scripts

A project depends on another project by loading a specific Coda file.

`build::load(path)` loads the file at `path`, executes it in the build context, and returns the exported targets from that file.

The load boundary is explicit in source code. Nothing is loaded implicitly because a directory contains files.

Example:

```coda
build::Target[] raylib = build::load("vendor/raylib/build.coda");
```

If the project has multiple build files, the user chooses the one to load. There is no canonical build filename enforced by the language.

## Acquisition workflow

Fetching a dependency is separate from loading it.

A typical workflow is:

1. Acquire source into `vendor/<name>/`.
2. Load the chosen build file from that directory.
3. Use the returned targets in the current build graph.

Example:

```coda
if (!fs::exists("vendor/raylib")) {
    pkg::clone("https://github.com/raysan5/raylib", "vendor/raylib");
    pkg::checkout_tag("vendor/raylib", "5.5");
}

build::Target[] raylib = build::load("vendor/raylib/build.coda");
```

## Trust model

Fetching does not execute code.

Loading a build script does execute code, but only when explicitly requested by the current build script.

That gives a clear boundary:

* `pkg::*` operations are untrusted data movement
* `build::load()` is explicit code execution
* dependency code never runs during acquisition

## Why this is useful

This model has one abstraction for everything:

* a project is a buildable unit
* a library is a buildable unit
* a vendored dependency is a buildable unit
* a build script is the authoritative description of that unit

That removes duplication and keeps the system small.

There is no separate resolver, no manifest language, no special package format, and no hidden dependency machinery.

## Minimal API sketch

`core::pkg`:

```coda
fn void pkg::clone(string url, string dst);
fn void pkg::checkout(string repo, string ref);
fn void pkg::download(string url, string dst);
fn void pkg::extract(string archive, string dst);
fn void pkg::copy(string src, string dst);
fn void pkg::link(string src, string dst);
```

`core::build`:

```coda
fn build::Target build::library(LibrarySpec spec);
fn build::Target build::executable(ExecutableSpec spec);
fn build::Target[] build::load(string path);
```

A build script is free to export any number of targets, and consumers decide which file to load and which returned targets to use.

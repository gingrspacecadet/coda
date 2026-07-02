# x86_64/Linux stdlib principles

* Everything allocates through an explicit allocator
* Formatting is handled exclusively by `std::fmt`
* `std::io` moves bytes. it does not interpret them
* `std::path` never touches the filesystem
* Convenience functions are layered on top of primitives
* standard library modules should compose rather than overlap
* every module should be usable without depending on a hidden global state.

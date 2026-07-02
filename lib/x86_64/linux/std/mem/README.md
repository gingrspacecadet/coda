# x86_64/Linux std::mem

An allocator returns raw bytes (`mut uint8[]`). The convenience function `fn T *allocate<T>(uint64 count, Allocator *a)` exists to return typed memory. It can and will panic if the underlying allocation strategy fails. For this reason, a `try_allocate` function is also provided.

`copy` does not allow overlapping regions

`move` does allow overlapping regions

`fill` and `zero` operate on bytes

`compare` reads from `0-n` in `lhs`, and returns the difference between first occurence of non-equal bytes.

`swap` operates on references, and swaps them byte-for-byte.

`align_up` and `align_down` must have power-of-2 alignment arguments. if they are not, the function will return the value unchanged.


# Typeid Specification

A runtime type is a value of type `#type*`. Types become `#type*`s at runtime. The compiler is responsible for ensuring that all identical types use the same pointers.

The rest of this document describes the format of a `#type`.

## Layout

All fields are little endian and packed contiguously in memory.

```coda
uint8 kind;
uint8 flags;
if named:
    string* name;
if generic:
    string*[] generic_names;
[type-dependent data]
```

## Flags

Bit 0 is the least-significant bit, bit 7 is the most-significant.

```
0 - named
1 - mutable
2 - generic
3 - reserved
4 - type dependent
5 - type dependent
6 - type dependent
7 - type dependent
```

## Type Kinds

### Basic types

```
00 - none
01 - null
02 - bool
03 - int8
04 - int16
05 - int32
06 - int64
07 - uint8
08 - uint16
09 - uint32
0A - uint64
0B - ssize
0C - usize
0D - float16
0E - float32
0F - float64
10 - any
```

### Intrinsics

```
11 - intrinsic

Fields:
string* intrinsic_name;
```

### Generics

```
12 - generic

Fields:
#type* original;
#type*[] values;
```

### Generic Parameters

```
13 - generic parameter
```

This type represents a generic parameter. It does not resolve to a real type by itself, but can only be used inside generic types. The `name` flag must be set to 1.

### Pointers

```
14 - pointer

Flags:
4 - mutable;
5 - optional;

Fields:
#type* to;
```

### Arrays

```
15 - array

Flags:
4 - mutable

Fields:
#type* of;
usize*? len;
```

### Functions

There has to be a separate "generic" field for this, because one can have generic type aliases for generic functions.

```
16 - function

Flags:
4 - generic

Fields:
if generic:
    string*[] fn_generic_names;
#type* return_type;
(struct {
    string* name;
    #type* type;
})[] parameters;
```

### Structs

```
17 - struct

Fields:
usize size;
(struct {
    string* name;
    #type* type;
    usize align;
})[] members;
```

### Unions

```
18 - union

Fields:
usize size;
(struct {
    string* name;
    #type* type;
    usize align;
})[] members;
```

### Enums

```
19 - enum

Fields:
#type* backing_type;
(struct {
    string* name;
    backing_type value;
})[] members;
```

### Sum types

```
1a - sum type

Fields:
usize size;
#type*[] members;
```

## Code Representation

To the compiler, they appear as a value of this format:

```coda
constraint #type = {
    uint8 kind;
    uint8 flags;
};
```

All other fields must be accessed by pointer arithmetic and casting.

Or, maybe they should have "magical" behavior where the accessible fields automatically take into account the flags.

## Any

A value of type `any` is essentially this:

```coda
type any = {
    #type* typeid;
    none* value;
};
```

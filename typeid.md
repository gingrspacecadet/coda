# Typeid Specification

A runtime type is a value of type `#type*`. Types become `#type*`s at runtime. The compiler is responsible for ensuring that all identical types use the same pointers.

The rest of section document describes the format of a `#type`.

## Layout

All fields are little endian.

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
02 - int8
03 - int16
04 - int32
05 - int64
06 - uint8
07 - uint16
08 - uint32
09 - uint64
0A - ssize
0B - usize
0C - float16
0D - float32
0E - float64
0F - any
```

### Intrinsics

```
10 - intrinsic

Fields:

string* intrinsic_name;
```

### Generics

```
11 - generic

Fields:

#type* original;
#type[] values;
```

### Pointers

```
12 - pointer

Flags:
4 - optional;
5 - mutable;

Fields:

#type* to;
```

### Arrays

```
13 - array

Fields:

#type* to;
usize*? len;
```

### Functions

There has to be a separate "generic" field for this, because one can have generic type aliases for generic functions.

```
14 - function

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
15 - struct

Fields:

(struct {
    string* name;
    #type* type;
})[] members;
```

### Unions

```
16 - union

Fields:

(struct {
    string* name;
    #type* type;
})[] members;
```

### Enums

```
17 - enum

Fields:

uint8 backing_type;
(struct {
    string* name;
    backing_type value;
})[] members;
```

The `backing_type` is specified the same as the type kinds, it must represent a primitive integer type, so one of 0x02 to 0x0E.

### Sum types

```
18 - sum type

Fields:

#type[] members;
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

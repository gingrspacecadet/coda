# TypeID Calculation Specification

## Layout

All fields are little endian.

```
[u8 node_type]
[u8 bucket] // residing hash bucket; for avoiding collisions
[u8 flags]  // bitfield: mutable, optional, etc
if named: [u32 name_len] [str name]
if pointer: [typeid child_hash]
if array: [typeid child_hash] [u32 length]
if fn: [typeid ret_hash] [u32 param_count] [typeid array param_hash]
```

## Hash Function and Mixing

Stream the serialised layout ([above](#Layout)) to the xxHash64 hasher.
If target is less than 64 bits, mix the 64 down to target bits (use splitmix64) then truncate.
Use the seed 0x\_\_DEADBEEF\_\_ for all hashing algorithms.

## Collision Detection and Resolution

The **Primary Id** is the finished hash.
Maintain a comptime map of ID to serialised layout.
When inserting a new type:
 * If ID not present, insert (ID, Desc) and return ID
 * If ID present and descriptor equals stored descriptor then return ID
 * If ID present and descriptors do not match, there is a collision. 

# module plan 
first, the pre-scan  
gather the search paths (defaults to cwd and stdlib path)  
recurse those directories for .coda files  
peek the module decl  
and if valid module, add to global map (otherwise silently ignore)  
then, on an include,   
lookup the module name in the global mapping  
if not found, error (i.e `Module "std::io" not found in include paths`)  
if found, fully parse the file (unless already parsed ofc)  
and dump decls into a namespace  
simple!  
ill also want module caches  
that have file path, module, mtime (last updated time), and exported decls  
and as an added bonus  
if mtime misses the cache  
hash the file, check if its contents changed, and if so, rebuild  
otherwise dont!  
also, cache design:  
```c
typedef struct {
  uint32_t magic;  // 0x434F4441 "CODA"
  uint32_t cache_version;
  uint32_t file_count;
  uint64_t string_pool_size;
} CacheHeader;

typedef struct {
  uint64_t mtime;
  uint64_t content_hash; // xxHash64

  uint32_t path_offset; // file path string
  uint32_t mod_offset; // module name string

  uint32_t decl_offset; // file's exported decls
  uint32_t decl_count; // number of exported symbols
} CacheFileRecord;

// string_pool data at decl_offset:
"fn int | FileError setup_network();\nfn none load_config<T: fn uint64 read(T mut *self, uint8[] buffer)>(T mut *source);"
```

we just store the string of the prototype instead of the parsed one cause that would suck  
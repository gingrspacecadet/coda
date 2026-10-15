#ifndef CODA_MANGLE_H
#define CODA_MANGLE_H

#include "sema.h"

typedef struct {
    char kind;
    Array(String) module_path;
    String name;
    String receiver;
    String signature;
    Array(String) generic_arguments;
} CodaDemangledSymbol;

String coda_mangle_symbol(Sema *sema, Symbol *symbol, Array arguments, bool specialization);

bool coda_demangle_symbol(Arena *arena, String mangled, CodaDemangledSymbol *symbol);
String coda_demangled_symbol_name(Arena *arena, const CodaDemangledSymbol *symbol);

#endif

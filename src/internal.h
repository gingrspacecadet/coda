#pragma once

#include "lexer.h"

static bool is_builtin_type_token(TokenType t) {
    switch (t) {
        case CHAR: case STRING:
        case INT: case INT8: case INT16: case INT32: case INT64:
        case UINT: case UINT8: case UINT16: case UINT32: case UINT64:
        case BOOL:
        case _NULL:
            return true;
        default:
            return false;
    }
}

static const char *builtin_name_for_token(TokenType t) {
    switch (t) {
        case CHAR:   return "char";
        case STRING: return "String";
        case INT:    return "int";
        case INT8:   return "int8";
        case INT16:  return "int16";
        case INT32:  return "int32";
        case INT64:  return "int64";
        case UINT:   return "uint";
        case UINT8:  return "uint8";
        case UINT16: return "uint16";
        case UINT32: return "uint32";
        case UINT64: return "uint64";
        case BOOL:   return "bool";
        case _NULL:  return "null";
        default:     return "<builtin>";
    }
}
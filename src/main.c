#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "lexer.h"
#include "parser.h"
#include "print.h"
#include "arena.h"

void pretty_print_tokens(TokenBuffer *tokens) {
    for (int i = 0; i < tokens->len; i++) {
        Token t = tokens->data[i];
        switch (t.type) {
            case MODULE: puts("MODULE"); break;
            case INCLUDE: puts("INCLUDE"); break;
            case FN: puts("FN"); break;
            case RETURN: puts("RETURN"); break;
            case IDENT: printf("IDENT %s\n", t.value); break;
            case ATTR: printf("ATTR %s\n", t.value); break;
            case MUT: puts("MUT"); break;

            case STRING: printf("STRING %s\n", t.value); break;
            case CHAR: printf("CHAR %s\n", t.value); break;
            case INT8: puts("INT8"); break;
            case INT16: puts("INT16"); break;
            case INT32: puts("INT32"); break;
            case INT64: puts("INT64"); break;
            case UINT8: puts("UINT8"); break;
            case UINT16: puts("UINT16"); break;
            case UINT32: puts("UINT32"); break;
            case UINT64: puts("UINT64"); break;
            case _NULL: puts("NULL"); break;

            case LPAREN: puts("LPAREN"); break;
            case RPAREN: puts("RPAREN"); break;
            case LBRACE: puts("LBRACE"); break;
            case RBRACE: puts("RBRACE"); break;
            case LBRACK: puts("LBRACK"); break;
            case RBRACK: puts("RBRACK"); break;
            case NUMBER: printf("NUMBER %s\n", t.value); break;
            case COLON: puts("COLON"); break;
            case SEMICOLON: puts("SEMICOLON"); break;
            case DOUBLECOLON: puts("DOUBLECOLON"); break;
            case COMMA: puts("COMMA"); break;
            case DOT: puts("DOT"); break;
            case AMP: puts("AMP"); break;
            case QUESTION: puts("QUESTION"); break;

            case PLUS: puts("PLUS"); break;
            case MINUS: puts("MINUS"); break;
            case STAR: puts("STAR"); break;
            case DIV: puts("DIV"); break;
            case LSHIFT: puts("LSHIFT"); break;
            case RSHIFT: puts("RSHIFT"); break;

            case GREATER: puts("GREATER"); break;
            case LESS: puts("LESS"); break;
            case EQ_EQ: puts("EQ_EQ"); break;
            case BANG_EQ: puts("BANG_EQ"); break;
            case GREATER_EQ: puts("GREATER_EQ"); break;
            case LESS_EQ: puts("LESS_EQ"); break;

            case EQ: puts("EQ"); break;
            case PLUS_EQ: puts("PLUS_EQ"); break;
            case MINUS_EQ: puts("MINUS_EQ"); break;
            case STAR_EQ: puts("STAR_EQ"); break;
            case DIV_EQ: puts("DIV_EQ"); break;
            case RSHIFT_EQ: puts("RSHIFT_EQ"); break;
            case LSHIFT_EQ: puts("LSHIFT_EQ"); break;

            case _EOF: puts("EOF"); break;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;

    FILE *src_file = fopen(argv[1], "r");
    fseek(src_file, 0, SEEK_END);
    size src_size = ftell(src_file);
    rewind(src_file);

    char *buf = malloc(src_size + 1);
    fread(buf, 1, src_size, src_file);
    buf[src_size] = '\0';
    TokenBuffer tokens = lexer(buf);
    pretty_print_tokens(&tokens);
    Arena *a = arena_create();
    Module *module = parser(&tokens, a);
    pretty_print_module(module);
    arena_destroy(a);

    return 0;
}
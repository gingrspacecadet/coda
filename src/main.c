#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include "lexer.h"

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

    for (int i = 0; i < tokens.idx; i++) {
        Token t = tokens.data[i];
        switch (t.type) {
            case MODULE: puts("MODULE"); break;
            case IDENT: printf("IDENT %s\n", t.value); free(t.value); break;
            case SEMI: puts("SEMI"); break;
            case FN: puts("FN"); break;
            case INT: puts("INT"); break;
            case LPAREN: puts("LPAREN"); break;
            case RPAREN: puts("RPAREN"); break;
            case LBRACE: puts("LBRACE"); break;
            case RBRACE: puts("RBRACE"); break;
            case RETURN: puts("RETURN"); break;
            case NUMBER: printf("NUMBER %s\n", t.value); free(t.value); break;
            case _EOF: puts("EOF"); break;
        }
    }

    return 0;
}
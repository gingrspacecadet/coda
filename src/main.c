#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "lexer.h"
#include "parser.h"

void print_program(Program *prog) {
    printf("Module: %s\n", prog->module.name);
    printf("Main returns: %d\n", prog->main_fn.ret);
}

void emit_expr(FILE *f, Expr *e) {
    if (e->type == EXPR_NUMBER) {
        fprintf(f, "%d", e->number);
    } else if (e->type = EXPR_BINOP) {
        emit_expr(f, e->binop.left);
        fprintf(f, " %c ", e->binop.op);
        emit_expr(f, e->binop.right);
    }
}

void emit_c(Program *prog, const char *out_path) {
    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "errorL cannot open %s for writing\n", out_path);
        exit(1);
    }

    fprintf(f, "#include <stdio.h>\n\n");

    fprintf(f, "int main(void) {\n    return ");
    emit_expr(f, prog->main_fn.ret);
    fprintf(f, ";\n}\n");

    fclose(f);
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

    for (int i = 0; i < tokens.len; i++) {
        Token t = tokens.data[i];
        switch (t.type) {
            case MODULE: puts("MODULE"); break;
            case INCLUDE: puts("INCLUDE"); break;
            case IDENT: printf("IDENT %s\n", t.value); break;
            case SEMI: puts("SEMI"); break;
            case FN: puts("FN"); break;
            case INT: puts("INT"); break;
            case LPAREN: puts("LPAREN"); break;
            case RPAREN: puts("RPAREN"); break;
            case LBRACE: puts("LBRACE"); break;
            case RBRACE: puts("RBRACE"); break;
            case RETURN: puts("RETURN"); break;
            case NUMBER: printf("NUMBER %s\n", t.value); break;
            case PLUS: puts("PLUS"); break;
            case MINUS: puts("MINUS"); break;
            case DOUBLECOLON: puts("DOUBLECOLON"); break;
            case _EOF: puts("EOF"); break;
        }
    }

    Program p = parse_program(&tokens);
    mkdir("out", 0755);
    emit_c(&p, "out/hello.c");
    printf("Generated out/hello.c\n");

    return 0;
}
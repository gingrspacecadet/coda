#include "lexer.h"
#include "parser.h"
#include "print.h"

int main() {
    Source s = {
        .contents = string_make(
            "module lambda;\n"
            "\n"
            "fn int test(fn int(int) func) {\n"
            "    return func(2);\n"
            "}\n"
            "\n"
            "@export\n"
            "fn int main() {\n"
            "    return test(fn int (int a) {\n"
            "        return a;\n"
            "    });\n"
            "}\n"
        )
    };

    Arena *arena = arena_create();

    source_build_lines(&s, arena);

    Diags d = {
        .arena = arena,
    };

    array_init(&d.diags, arena, sizeof(Diag));

    Lexer l = {
        .source = &s,
        .diags = &d,
    };

    Token t;
    do {
        t = lexer_next(&l);
        printf("%s", TokenTypeNames[t.type]);

        if (t.type == TK_STRING || t.type == TK_IDENT) {
            printf("    %.*s", (int)t.span.length, &t.span.source->contents.data[t.span.offset]);
        }


        putchar('\n');
    } while (t.type != TK_EOF);

    l.index = 0;

    Parser p;
    parser_init(&p, &l, arena);
    AstModule *m = parser_parse_module(&p);

    print_ast_module(stdout, m);
}
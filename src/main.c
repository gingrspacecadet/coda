#include "lexer.h"

int main() {
    printf("Hello, world!\n");

    Source s = {
        .contents = string_make(
            "1 + 2 * 3\n"
            "a = b = c\n"
            "foo(1, 2).bar[3]?\n"
            "foo<int>(x)\n"
            "(int32)x\n"
            "{x = 1, y = 2}\n"
            "#sizeof(int)\n"
            "#(name)\n"
            "$a + b\n"
            "-a * b\n"
            "a && b || c\n"
        )
    };

    Diags d = {
        .arena = arena_create(),
    };

    array_init(&d.diags, d.arena, sizeof(Diag));

    Lexer l = {
        .source = &s,
        .diags = &d,
    };

    Token t;
    do {
        t = lexer_next(&l);
        printf("%s\n", TokenTypeNames[t.type]);

        if (t.type == TK_STRING) {
            printf("    %.*s\n", (int)t.span.length, &t.span.source->contents.data[t.span.offset]);
        }
    } while (t.type != TK_EOF);
}
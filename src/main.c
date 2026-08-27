#include <time.h>
#include "lexer.h"
#include "parser.h"
#include "print.h"

#define BOLD_WHITE "\x1b[1;37m"
#define RED "\x1b[1;31m"
#define RESET "\x1b[0m"

void print_source_line(Span span) {
    if (span.source == NULL)
        return;

    Source *source = span.source;

    size_t start = span.offset;
    while (start > 0 &&
           source->contents.data[start - 1] != '\n') {
        start--;
    }

    size_t end = span.offset;
    while (end < source->contents.length &&
           source->contents.data[end] != '\n') {
        end++;
    }

    size_t line = source_line(source, span.offset);
    size_t column = span.offset - start;

    printf("  |\n");
    printf("%lu | %.*s\n",
        line,
        (int)(end - start),
        source->contents.data + start
    );

    printf("  | ");

    for (size_t i = 0; i < column; i++)
        putchar(' ');

    size_t width = span.length ? span.length : 1;

    printf(RED);
    for (size_t i = 0; i < width; i++)
        putchar(i == 0 ? '^' : '~');
    printf(RESET);

    putchar('\n');
}

void print_span_location(Span span) {
    printf("--> %.*s:%lu:%lu\n",
        string_fmt(
            span.source
                ? span.source->path
                : string_make("<no file>")
        ),
        source_line(span.source, span.offset),
        source_column(span.source, span.offset)
    );
}

void print_span(Span span) {
    print_span_location(span);
    print_source_line(span);
}

void print_label(DiagLabel label) {
    print_span(label.span);

    printf("%.*s\n", string_fmt(label.message));
}

void print_diags(Diags *diags) {
    if (diags_has_errors(diags)) {
        for (size_t i = 0; i < diags->diags.len; i++) {
            Diag *d = &((Diag*)diags->diags.data)[i];

            printf(RED "error[E%04d]" BOLD_WHITE ": %.*s\n\n" RESET, d->code, string_fmt(d->message));
            print_label(d->primary);


            for (size_t j = 0; j < d->labels.len; j++) {
                print_label(((DiagLabel*)d->labels.data)[j]);
            }
        }
    }
}

int main() {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    Source s = {
        .contents = string_make(
            "module lambda;\n"
            "\n"
            "fen int test(fn int(int) func) {\n"
            "    return func(2);\n"
            "}\n"
            "\n"
            "@export\n"
            "fn int main() {\n"
            "    return test(fn int (int a) {\n"
            "        return a;\n"
            "    });\n"
            "}\n"
        ),
        .path = string_make("<no file>")
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

    print_diags(l.diags);

    // Token t;
    // do {
    //     t = lexer_next(&l);
    //     printf("%s", TokenTypeNames[t.type]);

    //     if (t.type == TK_STRING || t.type == TK_IDENT) {
    //         printf("    %.*s", (int)t.span.length, &t.span.source->contents.data[t.span.offset]);
    //     }
    
    //     putchar('\n');
    // } while (t.type != TK_EOF);

    l.index = 0;

    Parser p;
    parser_init(&p, &l, arena);
    AstModule *m = parser_parse_module(&p);

    print_diags(p.diags);

    // print_ast_module(stdout, m);

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);

    printf("Compilation took %ld seconds (%ld nanoseconds)\n", end.tv_sec - start.tv_sec, end.tv_nsec - start.tv_nsec);
}
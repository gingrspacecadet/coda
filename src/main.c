#include <time.h>
#include "print.h"
#include "coda.h"

#define BOLD_WHITE "\x1b[1;37m"
#define RED "\x1b[1;31m"
#define CYAN "\x1b[1;36m"
#define YELLOW "\x1b[1;33m"
#define RESET "\x1b[0m"

static size_t source_line_start(Source *source, size_t line) {
    return ((size_t *)source->line_offsets.data)[line - 1];
}

static size_t source_line_end(Source *source, size_t line) {
    size_t start = source_line_start(source, line);
    size_t end = line < source->line_offsets.len
        ? source_line_start(source, line + 1)
        : source->contents.length;

    if (end > start && source->contents.data[end - 1] == '\n')
        end--;

    return end;
}

static void print_source_line_blank(size_t width) {
    printf("%*s | ", (int)width, "");
}

static void print_span_marker(Span span, size_t start, size_t end) {
    size_t span_start = span.offset;
    size_t span_end = span.offset + (span.length ? span.length : 1);

    if (span_start < start)
        span_start = start;

    if (span_end > end)
        span_end = end;

    if (span_start >= span_end)
        return;

    for (size_t i = start; i < span_start; i++)
        putchar(' ');

    putchar('^');

    for (size_t i = span_start + 1; i < span_end; i++)
        putchar('~');
}

void print_diags(Diags *diags) {
    for (size_t i = 0; i < diags->diags.len; i++) {
        Diag *diag = array_at(&diags->diags, i);
        Span primary = diag->primary.span;

        printf(
            RED "error[E%04d]" RESET ": %.*s\n",
            diag->code,
            string_fmt(diag->message)
        );

        if (primary.source == NULL)
            continue;

        Source *source = primary.source;
        size_t first = source_line(source, primary.offset);
        size_t last = first;

        for (size_t j = 0; j < diag->labels.len; j++) {
            DiagLabel *label = array_at(&diag->labels, j);

            if (label->span.source != source)
                continue;

            size_t line = source_line(source, label->span.offset);

            if (line < first)
                first = line;

            if (line > last)
                last = line;
        }

        size_t width = snprintf(NULL, 0, "%zu", last);

        printf(
            " --> %.*s:%zu:%zu\n",
            string_fmt(source->path),
            source_line(source, primary.offset),
            source_column(source, primary.offset)
        );

        fputs("  |\n", stdout);

        for (size_t line = first; line <= last; line++) {
            size_t start = source_line_start(source, line);
            size_t end = source_line_end(source, line);

            printf(
                "%*zu | %.*s\n",
                (int)width,
                line,
                (int)(end - start),
                source->contents.data + start
            );

            bool primary_here =
                source_line(source, primary.offset) == line;

            bool labels_here = false;

            for (size_t j = 0; j < diag->labels.len; j++) {
                DiagLabel *label = array_at(&diag->labels, j);

                if (label->span.source != source)
                    continue;

                size_t label_start = label->span.offset;
                size_t label_end =
                    label->span.offset +
                    (label->span.length ? label->span.length : 1);

                if (label_end > start && label_start < end) {
                    labels_here = true;
                    break;
                }
            }

            if (!primary_here && !labels_here)
                continue;

            if (primary_here) {
                print_source_line_blank(width);

                fputs(RED, stdout);
                print_span_marker(primary, start, end);
                fputs(RESET, stdout);

                printf(
                    RED " %.*s" RESET,
                    string_fmt(diag->primary.message)
                );

                putchar('\n');
            }

            for (size_t j = 0; j < diag->labels.len; j++) {
                DiagLabel *label = array_at(&diag->labels, j);
                Span span = label->span;

                if (span.source != source)
                    continue;

                size_t span_start = span.offset;
                size_t span_end =
                    span.offset +
                    (span.length ? span.length : 1);

                if (span_end <= start || span_start >= end)
                    continue;

                print_source_line_blank(width);

                fputs(CYAN, stdout);
                print_span_marker(span, start, end);
                fputs(RESET, stdout);

                printf(
                    CYAN " %.*s" RESET,
                    string_fmt(label->message)
                );

                putchar('\n');
            }
        }

        fputs("  |\n", stdout);

        for (size_t j = 0; j < diag->notes.len; j++) {
            String *note = array_at(&diag->notes, j);

            printf(
                "= note: %.*s\n",
                string_fmt(*note)
            );
        }

        for (size_t j = 0; j < diag->help.len; j++) {
            String *help = array_at(&diag->help, j);

            printf(
                "= help: %.*s\n",
                string_fmt(*help)
            );
        }

        if (i + 1 < diags->diags.len)
            putchar('\n');
    }
}

static bool load_source(const char *path, Source *source) {
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        perror(path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long size = ftell(file);

    if (size < 0) {
        fclose(file);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    char *contents = malloc((size_t)size + 1);

    if (contents == NULL) {
        fclose(file);
        return false;
    }

    if (fread(contents, 1, (size_t)size, file) != (size_t)size) {
        free(contents);
        fclose(file);
        return false;
    }

    fclose(file);

    contents[size] = '\0';

    *source = (Source) {
        .path = (String) {
            .data = (char *)path,
            .length = strlen(path),
        },
        .contents = (String) {
            .data = contents,
            .length = (size_t)size,
        },
    };

    return true;
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 1;

    Arena *arena = arena_create();
    Diags diags = {.arena = arena, .diags = array_create(arena, sizeof(Diag))};

    Source source;
    load_source(argv[1], &source);
    CodaCompiler compiler;

    coda_compiler_init(&compiler, arena, &diags);

    if (!coda_compile(&compiler, &source, CODA_STAGE_LIR)) {
        print_diags(&diags);
        return 1;
    }

    print_ast_module(stdout, compiler.compilation.ast);
    print_hir_module(stdout, compiler.compilation.hir);
    lir_print(stdout, compiler.compilation.lir);

    return 0;
}
#include <stdio.h>
#include "lexer.h"
#include "parser.h"

// fprintf(stderr, "%.*s", len, str); to print String structures!

INSTANTIATE(Token, token, OPTIONAL_TEMPLATE)
INSTANTIATE(char, ARRAY_TEMPLATE)

static Source err_source = {0};

static token_optional peek(Parser *ctx) {
    if (ctx->index >= ctx->tokens.len) return (token_optional){};
    return (token_optional){true, ctx->tokens.data[ctx->index]};
}

#define BOLD_WHITE "\e[1;37m"
#define RED "\e[1;31m"
#define RESET "\e[0m"

void error_set_source(Source source) {
    err_source = source;
}

__attribute__((noreturn)) void error_parser(Parser *ctx, const char *msg) {
    token_optional t = peek(ctx);

    if (!t.has_value) {
        printf(BOLD_WHITE "%.*s:0:0: " RED "error: " RESET "%s\n" 
               RED "error: " RESET "at end of file\n", 
               err_source.path.length, err_source.path.data, msg);
        exit(1);
    }

    String file_view = err_source.contents;
    size_t span_start = t.value.span.start;
    size_t span_len = t.value.span.length;

    size_t line_start = span_start;
    while (line_start > 0 && file_view.data[line_start - 1] != '\n') --line_start;
    size_t line_end = span_start;
    while (line_end < file_view.length && file_view.data[line_end] != '\n') ++line_end;

    String line_view = (String){
        .data = file_view.data + line_start,
        .length = line_end - line_start
    };

    size_t col = t.value.col ? t.value.col : (span_start - line_start + 1);

    char_array printable_line = char_array_init();
    char_array_resize(&printable_line, line_view.length);
    for (size_t i = 0; i < line_view.length; i++) {
        char c = line_view.data[i];
        if (c == '\t') char_array_append(&printable_line, 4, ' ');
        else char_array_push(&printable_line, c);
    }
    char_array_push(&printable_line, '\0');

    size_t caret_pos = 0;
    for (size_t i = 0; i < (span_start - line_start); i++) {
        if (file_view.data[line_start + i] == '\t') {
            caret_pos += 4;
        } else {
            caret_pos++;
        }
    }

    size_t caret_len = 1;
    if (span_len > 0) {
        size_t cols = 0;
        for (size_t i = 0; i < span_len && (line_start + (span_start - line_start) + i) < line_end; i++) {
            char c = file_view.data[span_start + i];
            cols += (c == '\t') ? 4 : 1;
        }
        caret_len = cols > 1 ? cols : 1;
    }

    printf(BOLD_WHITE "%.*s:%ld:%ld: " RED "error: " RESET "%s\n", err_source.path.length, err_source.path.data, t.value.line, col, msg);

    printf("%ld | %s\n", t.value.line, printable_line.data);

    size_t line_num_len = snprintf(NULL, 0, "%ld", t.value.line);

    char_array underline = char_array_init();
    char_array_append(&underline, line_num_len, ' ');
    char_array_push(&underline, ' ');
    char_array_push(&underline, '|');
    char_array_push(&underline, ' ');
    char_array_push(&underline, '\e');
    char_array_push(&underline, '[');
    char_array_push(&underline, '1');
    char_array_push(&underline, ';');
    char_array_push(&underline, '3');
    char_array_push(&underline, '1');
    char_array_push(&underline, 'm');
    char_array_append(&underline, caret_pos, ' ');
    char_array_push(&underline, '^');
    char_array_append(&underline, caret_len - 1, '~');
    char_array_push(&underline, '\0');

    printf("%s" RESET "\n", underline.data);

    exit(1);
}

__attribute__((noreturn)) void error_sema(Token t, const char *msg) {
    String file_view = err_source.contents;
    size_t span_start = t.span.start;
    size_t span_len = t.span.length;

    size_t line_start = span_start;
    while (line_start > 0 && file_view.data[line_start - 1] != '\n') --line_start;
    size_t line_end = span_start;
    while (line_end < file_view.length && file_view.data[line_end] != '\n') ++line_end;

    String line_view = (String){
        .data = file_view.data + line_start,
        .length = line_end - line_start
    };

    size_t col = t.col ? t.col : (span_start - line_start + 1);

    char_array printable_line = char_array_init();
    char_array_resize(&printable_line, line_view.length);
    for (size_t i = 0; i < line_view.length; i++) {
        char c = line_view.data[i];
        if (c == '\t') char_array_append(&printable_line, 4, ' ');
        else char_array_push(&printable_line, c);
    }
    char_array_push(&printable_line, '\0');

    size_t caret_pos = 0;
    for (size_t i = 0; i < (span_start - line_start); i++) {
        if (file_view.data[line_start + i] == '\t') {
            caret_pos += 4;
        } else {
            caret_pos++;
        }
    }

    size_t caret_len = 1;
    if (span_len > 0) {
        size_t cols = 0;
        for (size_t i = 0; i < span_len && (line_start + (span_start - line_start) + i) < line_end; i++) {
            char c = file_view.data[span_start + i];
            cols += (c == '\t') ? 4 : 1;
        }
        caret_len = cols > 1 ? cols : 1;
    }

    printf(BOLD_WHITE "%.*s:%ld:%ld: " RED "error: " RESET "%s\n", err_source.path.length, err_source.path.data, t.line, col, msg);

    printf("%ld | %s\n", t.line, printable_line.data);

    size_t line_num_len = snprintf(NULL, 0, "%ld", t.line);

    char_array underline = char_array_init();
    char_array_append(&underline, line_num_len, ' ');
    char_array_push(&underline, ' ');
    char_array_push(&underline, '|');
    char_array_push(&underline, ' ');
    char_array_push(&underline, '\e');
    char_array_push(&underline, '[');
    char_array_push(&underline, '1');
    char_array_push(&underline, ';');
    char_array_push(&underline, '3');
    char_array_push(&underline, '1');
    char_array_push(&underline, 'm');
    char_array_append(&underline, caret_pos, ' ');
    char_array_push(&underline, '^');
    char_array_append(&underline, caret_len - 1, '~');
    char_array_push(&underline, '\0');

    printf("%s" RESET "\n", underline.data);

    exit(1);
}

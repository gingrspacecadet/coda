#include "../print.h"

#include <limits.h>
#include <unistd.h>
#include <string.h>

#define RED "\x1b[1;31m"
#define CYAN "\x1b[1;36m"
#define RESET "\x1b[0m"

static String source_display_path(Source *source, const char *cwd) {
    String path = source->path;

    if (cwd == NULL || path.data == NULL || path.length == 0 || path.data[0] != '/')
        return path;

    size_t cwd_length = strlen(cwd);
    while (cwd_length > 1 && cwd[cwd_length - 1] == '/')
        cwd_length--;

    if (cwd_length == 1 && cwd[0] == '/') {
        if (path.length > 1 && path.data[0] == '/') {
            path.data++;
            path.length--;
        }
        return path;
    }

    if (path.length <= cwd_length ||
        memcmp(path.data, cwd, cwd_length) != 0 ||
        path.data[cwd_length] != '/')
        return path;

    path.data += cwd_length + 1;
    path.length -= cwd_length + 1;
    return path;
}

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

static void print_source_gutter(size_t width) {
    printf("%*s |\n", (int)width, "");
}

static void span_bounds(Span span, Source *source, size_t *start, size_t *end) {
    size_t length = source->contents.length;

    *start = span.offset > length ? length : span.offset;
    *end = *start;

    if (span.length > length - *start)
        *end = length;
    else
        *end += span.length;
}

static bool span_line_range(Span span, Source *source, size_t *first, size_t *last) {
    if (source == NULL || source->line_offsets.len == 0 || span.source != source)
        return false;

    size_t start, end;
    span_bounds(span, source, &start, &end);

    *first = source_line(source, start);
    *last = source_line(source, end > start ? end - 1 : start);
    return true;
}

static void print_span_marker(Span span, Source *source, size_t start, size_t end) {
    size_t span_start, span_end;
    span_bounds(span, source, &span_start, &span_end);

    size_t marker_start = span_start < start ? start : span_start;
    if (marker_start > end)
        return;

    size_t marker_end = span_end > end ? end : span_end;
    if (marker_end < marker_start)
        marker_end = marker_start;

    for (size_t i = start; i < marker_start; i++)
        putchar(' ');

    putchar('^');

    for (size_t i = marker_start + 1; i < marker_end; i++)
        putchar('~');
}

static size_t diag_gutter_width(Diag *diag) {
    size_t width = 1;
    size_t first, last;

    if (span_line_range(diag->primary.span, diag->primary.span.source, &first, &last)) {
        size_t line_width = (size_t)snprintf(NULL, 0, "%zu", last);
        if (line_width > width)
            width = line_width;
    }

    for (size_t i = 0; i < diag->labels.len; i++) {
        DiagLabel *label = array_at(&diag->labels, i);
        if (!span_line_range(label->span, label->span.source, &first, &last))
            continue;

        size_t line_width = (size_t)snprintf(NULL, 0, "%zu", last);
        if (line_width > width)
            width = line_width;
    }

    return width;
}

static void print_diag_source(Diag *diag, Source *source, size_t width, const char *cwd) {
    if (source == NULL || source->line_offsets.len == 0)
        return;

    Span primary = diag->primary.span;
    bool has_primary = primary.source == source;
    Span anchor = primary;
    bool have_anchor = has_primary;

    size_t first = 0;
    size_t last = 0;
    bool have_lines = false;

    if (has_primary) {
        size_t span_first, span_last;
        if (span_line_range(primary, source, &span_first, &span_last)) {
            first = span_first;
            last = span_last;
            have_lines = true;
        }
    }

    for (size_t i = 0; i < diag->labels.len; i++) {
        DiagLabel *label = array_at(&diag->labels, i);
        if (label->span.source != source)
            continue;

        size_t span_first, span_last;
        if (!span_line_range(label->span, source, &span_first, &span_last))
            continue;

        if (!have_anchor) {
            anchor = label->span;
            have_anchor = true;
        }

        if (!have_lines) {
            first = span_first;
            last = span_last;
            have_lines = true;
        } else {
            if (span_first < first)
                first = span_first;
            if (span_last > last)
                last = span_last;
        }
    }

    if (!have_anchor || !have_lines)
        return;

    size_t anchor_offset = anchor.offset > source->contents.length
        ? source->contents.length
        : anchor.offset;

    String display_path = source_display_path(source, cwd);
    printf(
        " --> %.*s:%zu:%zu\n",
        string_fmt(display_path),
        source_line(source, anchor_offset),
        source_column(source, anchor_offset)
    );
    print_source_gutter(width);

    for (size_t line = first;; line++) {
        size_t start = source_line_start(source, line);
        size_t end = source_line_end(source, line);

        printf("%*zu | ", (int)width, line);
        if (end > start)
            fwrite(source->contents.data + start, 1, end - start, stdout);
        putchar('\n');

        size_t primary_first = 0;
        size_t primary_last = 0;
        if (has_primary && span_line_range(primary, source, &primary_first, &primary_last) &&
            line >= primary_first && line <= primary_last) {
            print_source_line_blank(width);
            fputs(RED, stdout);
            print_span_marker(primary, source, start, end);
            fputs(RESET, stdout);

            if (line == primary_first)
                printf(RED " %.*s" RESET, string_fmt(diag->primary.message));
            putchar('\n');
        }

        for (size_t i = 0; i < diag->labels.len; i++) {
            DiagLabel *label = array_at(&diag->labels, i);
            if (label->span.source != source)
                continue;

            size_t span_first, span_last;
            if (!span_line_range(label->span, source, &span_first, &span_last) ||
                line < span_first || line > span_last)
                continue;

            print_source_line_blank(width);
            fputs(CYAN, stdout);
            print_span_marker(label->span, source, start, end);
            fputs(RESET, stdout);

            if (line == span_first)
                printf(CYAN " %.*s" RESET, string_fmt(label->message));
            putchar('\n');
        }

        if (line == last)
            break;
    }

    print_source_gutter(width);
}

static bool diag_source_seen_before(Diag *diag, Source *source, size_t label_index) {
    if (diag->primary.span.source == source)
        return true;

    for (size_t i = 0; i < label_index; i++) {
        DiagLabel *label = array_at(&diag->labels, i);
        if (label->span.source == source)
            return true;
    }

    return false;
}

void print_diags(Diags *diags) {
    char cwd[PATH_MAX];
    const char *working_directory = getcwd(cwd, sizeof(cwd)) != NULL ? cwd : NULL;

    for (size_t i = 0; i < diags->diags.len; i++) {
        Diag *diag = array_at(&diags->diags, i);
        Source *primary_source = diag->primary.span.source;
        size_t gutter_width = diag_gutter_width(diag);

        printf(RED "error[E%04d]" RESET ": %.*s\n", diag->code, string_fmt(diag->message));

        if (primary_source != NULL)
            print_diag_source(diag, primary_source, gutter_width, working_directory);

        for (size_t j = 0; j < diag->labels.len; j++) {
            DiagLabel *label = array_at(&diag->labels, j);
            Source *source = label->span.source;

            if (source == NULL || diag_source_seen_before(diag, source, j))
                continue;

            print_diag_source(diag, source, gutter_width, working_directory);
        }

        for (size_t j = 0; j < diag->notes.len; j++) {
            String *note = array_at(&diag->notes, j);
            printf("= note: %.*s\n", string_fmt(*note));
        }

        for (size_t j = 0; j < diag->help.len; j++) {
            String *help = array_at(&diag->help, j);
            printf("= help: %.*s\n", string_fmt(*help));
        }

        if (i + 1 < diags->diags.len)
            putchar('\n');
    }
}

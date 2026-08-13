#include "lexer.h"

#include <string.h>

static bool is_ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z');
}

static bool is_identifier_start(char c) {
    return is_ascii_alpha(c) || c == '_';
}

static bool is_identifier_continue(char c) {
    return is_identifier_start(c) ||
           (c >= '0' && c <= '9');
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static bool at_end(const Lexer *lexer) {
    return lexer->pos >= lexer->source->contents.length;
}

static char peek(const Lexer *lexer) {
    if (at_end(lexer))
        return '\0';

    return lexer->source->contents.data[lexer->pos];
}

static char peek_next(const Lexer *lexer) {
    if (lexer->pos + 1 >= lexer->source->contents.length)
        return '\0';

    return lexer->source->contents.data[lexer->pos + 1];
}

static char advance_char(Lexer *lexer) {
    char c = peek(lexer);

    if (c == '\0')
        return '\0';

    lexer->pos++;

    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }

    return c;
}

static bool match_char(Lexer *lexer, char expected) {
    if (peek(lexer) != expected)
        return false;

    advance_char(lexer);
    return true;
}

static void lexer_error(Lexer *lexer, LexerErrorKind kind, size_t start, size_t start_line, size_t start_column, const char *message) {
    if (!lexer->error)
        return;

    Span span = {
        .source = lexer->source,
        .offset = start,
        .length = lexer->pos - start,
        .line = start_line,
        .column = start_column,
    };

    String text = {
        .data = (char *)message,
        .length = strlen(message),
    };

    lexer->error(lexer->error_userdata, kind, span, text);
}

static Token make_token(Lexer *lexer, TokenKind kind, size_t start, size_t line, size_t column) {
    Token token = {
        .kind = kind,
        .text = {
            .data = lexer->source->contents.data + start,
            .length = lexer->pos - start,
        },
        .span = {
            .source = lexer->source,
            .offset = start,
            .length = lexer->pos - start,
            .line = line,
            .column = column,
        },
    };

    return token;
}

static Token make_eof(Lexer *lexer) {
    Token token = {
        .kind = TOKEN_EOF,
        .text = {
            .data = lexer->source->contents.data + lexer->pos,
            .length = 0,
        },
        .span = {
            .source = lexer->source,
            .offset = lexer->pos,
            .length = 0,
            .line = lexer->line,
            .column = lexer->column,
        },
    };

    return token;
}

static TokenKind keyword_kind(String text) {

#define KW(name, kind) \
    if (text.length == sizeof(name) - 1 && \
        memcmp(text.data, name, sizeof(name) - 1) == 0) \
        return kind

    KW("module",    TOKEN_KW_MODULE);
    KW("include",   TOKEN_KW_INCLUDE);
    KW("type",      TOKEN_KW_TYPE);
    KW("struct",    TOKEN_KW_STRUCT);
    KW("union",     TOKEN_KW_UNION);
    KW("enum",      TOKEN_KW_ENUM);
    KW("fn",        TOKEN_KW_FN);
    KW("return",    TOKEN_KW_RETURN);
    KW("if",        TOKEN_KW_IF);
    KW("else",      TOKEN_KW_ELSE);
    KW("match",     TOKEN_KW_MATCH);
    KW("for",       TOKEN_KW_FOR);
    KW("while",     TOKEN_KW_WHILE);
    KW("break",     TOKEN_KW_BREAK);
    KW("continue",  TOKEN_KW_CONTINUE);
    KW("defer",     TOKEN_KW_DEFER);
    KW("mut",       TOKEN_KW_MUT);
    KW("true",      TOKEN_KW_TRUE);
    KW("false",     TOKEN_KW_FALSE);
    KW("none",      TOKEN_KW_NONE);

#undef KW

    return TOKEN_IDENTIFIER;
}


static Token lex_identifier(Lexer *lexer, size_t start, size_t line, size_t column) {
    while (is_identifier_continue(peek(lexer)))
        advance_char(lexer);

    Token token = make_token(lexer, TOKEN_IDENTIFIER, start, line, column);

    token.kind = keyword_kind(token.text);
    return token;
}

static Token invalid_number(Lexer *lexer, size_t start, size_t line, size_t column ) {
    while (is_identifier_continue(peek(lexer)))
        advance_char(lexer);

    lexer_error(lexer, LEXER_ERROR_INVALID_NUMBER, start, line, column, "invalid numeric literal");

    return make_token(lexer, TOKEN_INVALID, start, line, column);
}

static Token lex_number(Lexer *lexer, size_t start, size_t line, size_t column) {
    bool floating = false;

    if (peek(lexer) == '0') {
        char next = peek_next(lexer);

        if (next == 'x' || next == 'X') {
            advance_char(lexer);
            advance_char(lexer);

            size_t digits = 0;

            while (is_hex_digit(peek(lexer)) || peek(lexer) == '_') {
                if (peek(lexer) != '_')
                    digits++;

                advance_char(lexer);
            }

            if (digits == 0)
                return invalid_number(lexer, start, line, column);

            if (is_identifier_continue(peek(lexer)))
                return invalid_number(lexer, start, line, column);

            return make_token(
                lexer,
                TOKEN_INTEGER,
                start,
                line,
                column
            );
        }

        if (next == 'b' || next == 'B') {
            advance_char(lexer);
            advance_char(lexer);

            size_t digits = 0;

            while (peek(lexer) == '0' || peek(lexer) == '1' || peek(lexer) == '_') {
                if (peek(lexer) != '_')
                    digits++;

                advance_char(lexer);
            }

            if (digits == 0)
                return invalid_number(lexer, start, line, column);

            if (is_identifier_continue(peek(lexer)))
                return invalid_number(lexer, start, line, column);

            return make_token(
                lexer,
                TOKEN_INTEGER,
                start,
                line,
                column
            );
        }

        if (next == 'o' || next == 'O') {
            advance_char(lexer);
            advance_char(lexer);

            size_t digits = 0;

            while ((peek(lexer) >= '0' && peek(lexer) <= '7') ||
                   peek(lexer) == '_') {
                if (peek(lexer) != '_')
                    digits++;

                advance_char(lexer);
            }

            if (digits == 0)
                return invalid_number(lexer, start, line, column);

            if (is_identifier_continue(peek(lexer)))
                return invalid_number(lexer, start, line, column);

            return make_token(
                lexer,
                TOKEN_INTEGER,
                start,
                line,
                column
            );
        }
    }

    while (is_digit(peek(lexer)) || peek(lexer) == '_')
        advance_char(lexer);

    if (peek(lexer) == '.' && is_digit(peek_next(lexer))) {
        floating = true;
        advance_char(lexer);

        while (is_digit(peek(lexer)) || peek(lexer) == '_')
            advance_char(lexer);
    }

    if (peek(lexer) == 'e' || peek(lexer) == 'E') {
        floating = true;
        advance_char(lexer);

        if (peek(lexer) == '+' || peek(lexer) == '-')
            advance_char(lexer);

        size_t digits = 0;

        while (is_digit(peek(lexer)) || peek(lexer) == '_') {
            if (peek(lexer) != '_')
                digits++;

            advance_char(lexer);
        }

        if (digits == 0)
            return invalid_number(lexer, start, line, column);
    }

    if (is_identifier_continue(peek(lexer)))
        return invalid_number(lexer, start, line, column);

    return make_token(lexer, floating ? TOKEN_FLOAT : TOKEN_INTEGER, start, line, column);
}

static bool consume_escape(Lexer *lexer) {
    if (!match_char(lexer, '\\'))
        return false;

    char c = peek(lexer);

    switch (c) {
    case '\\':
    case '"':
    case '\'':
    case 'n':
    case 'r':
    case 't':
    case '0':
        advance_char(lexer);
        return true;

    case 'x':
        advance_char(lexer);

        if (!is_hex_digit(peek(lexer)) ||
            !is_hex_digit(peek_next(lexer))) {
            return false;
        }

        advance_char(lexer);
        advance_char(lexer);
        return true;

    default:
        return false;
    }
}

static Token lex_string(Lexer *lexer, size_t start, size_t line, size_t column) {
    advance_char(lexer);

    while (!at_end(lexer)) {
        char c = peek(lexer);

        if (c == '"') {
            advance_char(lexer);

            return make_token(lexer, TOKEN_STRING, start, line, column);
        }

        if (c == '\n' || c == '\r') {
            lexer_error(lexer, LEXER_ERROR_UNTERMINATED_STRING, start, line, column, "unterminated string literal");

            return make_token(lexer, TOKEN_INVALID, start, line, column);
        }

        if (c == '\\') {
            if (!consume_escape(lexer)) {
                lexer_error(lexer, LEXER_ERROR_INVALID_ESCAPE, start, line, column, "invalid escape sequence");

                while (!at_end(lexer) && peek(lexer) != '"')
                    advance_char(lexer);

                if (peek(lexer) == '"')
                    advance_char(lexer);

                return make_token(lexer, TOKEN_INVALID, start, line, column);
            }

            continue;
        }

        advance_char(lexer);
    }

    lexer_error(lexer, LEXER_ERROR_UNTERMINATED_STRING, start, line, column, "unterminated string literal");

    return make_token(lexer, TOKEN_INVALID, start, line, column);
}

static bool consume_char_literal_body(Lexer *lexer) {
    if (peek(lexer) == '\\')
        return consume_escape(lexer);

    if (peek(lexer) == '\0' || peek(lexer) == '\n' || peek(lexer) == '\r' || peek(lexer) == '\'')
        return false;

    // utf-8 nonsense
    unsigned char c = (unsigned char)peek(lexer);

    if (c < 0x80) {
        advance_char(lexer);
        return true;
    }

    if ((c & 0xE0) == 0xC0) {
        advance_char(lexer);

        for (int i = 0; i < 1; i++) {
            unsigned char next = (unsigned char)peek(lexer);
            if ((next & 0xC0) != 0x80)
                return false;
            advance_char(lexer);
        }

        return true;
    }

    if ((c & 0xF0) == 0xE0) {
        advance_char(lexer);

        for (int i = 0; i < 2; i++) {
            unsigned char next = (unsigned char)peek(lexer);
            if ((next & 0xC0) != 0x80)
                return false;
            advance_char(lexer);
        }

        return true;
    }

    if ((c & 0xF8) == 0xF0) {
        advance_char(lexer);

        for (int i = 0; i < 3; i++) {
            unsigned char next = (unsigned char)peek(lexer);
            if ((next & 0xC0) != 0x80)
                return false;
            advance_char(lexer);
        }

        return true;
    }

    return false;
}

static Token lex_char(Lexer *lexer, size_t start, size_t line, size_t column) {
    advance_char(lexer);

    if (!consume_char_literal_body(lexer)) {
        lexer_error(lexer, LEXER_ERROR_INVALID_CHARACTER_LITERAL, start, line, column, "invalid character literal");

        while (!at_end(lexer) && peek(lexer) != '\'')
            advance_char(lexer);

        if (peek(lexer) == '\'')
            advance_char(lexer);

        return make_token(lexer, TOKEN_INVALID, start, line, column);
    }

    if (peek(lexer) != '\'') {
        lexer_error(lexer, LEXER_ERROR_INVALID_CHARACTER_LITERAL, start, line, column, "character literal must contain exactly one character");

        while (!at_end(lexer) && peek(lexer) != '\'')
            advance_char(lexer);

        if (peek(lexer) == '\'')
            advance_char(lexer);

        return make_token(lexer, TOKEN_INVALID, start, line, column);
    }

    advance_char(lexer);

    return make_token(lexer, TOKEN_CHAR, start, line, column);
}

static bool skip_line_comment(Lexer *lexer) {
    if (peek(lexer) != '/' || peek_next(lexer) != '/')
        return false;

    advance_char(lexer);
    advance_char(lexer);

    while (!at_end(lexer) && peek(lexer) != '\n')
        advance_char(lexer);

    return true;
}

static bool skip_block_comment(Lexer *lexer) {
    if (peek(lexer) != '/' || peek_next(lexer) != '*')
        return false;

    size_t start = lexer->pos;
    size_t line = lexer->line;
    size_t column = lexer->column;

    advance_char(lexer);
    advance_char(lexer);

    size_t depth = 1;

    while (!at_end(lexer)) {
        if (peek(lexer) == '/' && peek_next(lexer) == '*') {
            advance_char(lexer);
            advance_char(lexer);
            depth++;
            continue;
        }

        if (peek(lexer) == '*' && peek_next(lexer) == '/') {
            advance_char(lexer);
            advance_char(lexer);

            depth--;

            if (depth == 0)
                return true;

            continue;
        }

        advance_char(lexer);
    }

    lexer_error(lexer, LEXER_ERROR_UNTERMINATED_COMMENT, start, line, column, "unterminated block comment");

    return true;
}

static void skip_trivia(Lexer *lexer) {
    for (;;) {
        bool skipped = false;

        while (!at_end(lexer)) {
            char c = peek(lexer);

            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                break;
            }

            advance_char(lexer);
            skipped = true;
        }

        if (skip_line_comment(lexer)) {
            skipped = true;
            continue;
        }

        if (skip_block_comment(lexer)) {
            skipped = true;
            continue;
        }

        if (!skipped)
            return;
    }
}

static TokenKind lex_operator(Lexer *lexer, char c) {
    switch (c) {
    case '(':
        advance_char(lexer);
        return TOKEN_LPAREN;

    case ')':
        advance_char(lexer);
        return TOKEN_RPAREN;

    case '{':
        advance_char(lexer);
        return TOKEN_LBRACE;

    case '}':
        advance_char(lexer);
        return TOKEN_RBRACE;

    case '[':
        advance_char(lexer);
        return TOKEN_LBRACKET;

    case ']':
        advance_char(lexer);
        return TOKEN_RBRACKET;

    case ',':
        advance_char(lexer);
        return TOKEN_COMMA;

    case ';':
        advance_char(lexer);
        return TOKEN_SEMICOLON;

    case ':':
        advance_char(lexer);

        if (match_char(lexer, ':'))
            return TOKEN_DOUBLE_COLON;

        return TOKEN_COLON;

    case '.':
        advance_char(lexer);
        return TOKEN_DOT;

    case '@':
        advance_char(lexer);
        return TOKEN_AT;

    case '#':
        advance_char(lexer);
        return TOKEN_HASH;

    case '$':
        advance_char(lexer);
        return TOKEN_DOLLAR;

    case '?':
        advance_char(lexer);
        return TOKEN_QUESTION;

    case '+':
        advance_char(lexer);

        if (match_char(lexer, '+'))
            return TOKEN_PLUS_PLUS;

        if (match_char(lexer, '='))
            return TOKEN_PLUS_EQUAL;

        return TOKEN_PLUS;

    case '-':
        advance_char(lexer);

        if (match_char(lexer, '>'))
            return TOKEN_ARROW;

        if (match_char(lexer, '-'))
            return TOKEN_MINUS_MINUS;

        if (match_char(lexer, '='))
            return TOKEN_MINUS_EQUAL;

        return TOKEN_MINUS;

    case '*':
        advance_char(lexer);

        if (match_char(lexer, '='))
            return TOKEN_STAR_EQUAL;

        return TOKEN_STAR;

    case '/':
        advance_char(lexer);

        if (match_char(lexer, '='))
            return TOKEN_SLASH_EQUAL;

        return TOKEN_SLASH;

    case '%':
        advance_char(lexer);

        if (match_char(lexer, '='))
            return TOKEN_PERCENT_EQUAL;

        return TOKEN_PERCENT;

    case '=':
        advance_char(lexer);

        if (match_char(lexer, '='))
            return TOKEN_EQUAL_EQUAL;

        return TOKEN_EQUAL;

    case '!':
        advance_char(lexer);

        if (match_char(lexer, '=')) {
            return TOKEN_BANG_EQUAL;
        }

        if (match_char(lexer, '&')) {
            if (match_char(lexer, '='))
                return TOKEN_BANG_AMPERSAND_EQUAL;

            return TOKEN_BANG_AMPERSAND;
        }

        return TOKEN_BANG;

    case '<':
        advance_char(lexer);

        if (match_char(lexer, '<')) {
            if (match_char(lexer, '='))
                return TOKEN_LSHIFT_EQUAL;

            return TOKEN_LSHIFT;
        }

        if (match_char(lexer, '='))
            return TOKEN_LT_EQUAL;

        return TOKEN_LT;

    case '>':
        advance_char(lexer);

        if (match_char(lexer, '>')) {
            if (match_char(lexer, '='))
                return TOKEN_RSHIFT_EQUAL;

            return TOKEN_RSHIFT;
        }

        if (match_char(lexer, '='))
            return TOKEN_GT_EQUAL;

        return TOKEN_GT;

    case '&':
        advance_char(lexer);

        if (match_char(lexer, '&'))
            return TOKEN_LOGICAL_AND;

        if (match_char(lexer, '='))
            return TOKEN_AMPERSAND_EQUAL;

        return TOKEN_AMPERSAND;

    case '|':
        advance_char(lexer);

        if (match_char(lexer, '|'))
            return TOKEN_LOGICAL_OR;

        if (match_char(lexer, '='))
            return TOKEN_PIPE_EQUAL;

        return TOKEN_PIPE;

    case '^':
        advance_char(lexer);

        if (match_char(lexer, '='))
            return TOKEN_CARET_EQUAL;

        return TOKEN_CARET;

    case '~':
        advance_char(lexer);

        if (match_char(lexer, '|')) {
            if (match_char(lexer, '='))
                return TOKEN_TILDE_PIPE_EQUAL;

            return TOKEN_TILDE_PIPE;
        }

        return TOKEN_TILDE;

    default:
        return TOKEN_INVALID;
    }
}

Token lexer_next(Lexer *lexer) {
    skip_trivia(lexer);

    if (at_end(lexer))
        return make_eof(lexer);

    size_t start = lexer->pos;
    size_t line = lexer->line;
    size_t column = lexer->column;

    char c = peek(lexer);

    if (is_identifier_start(c))
        return lex_identifier(lexer, start, line, column);

    if (is_digit(c))
        return lex_number(lexer, start, line, column);

    if (c == '"')
        return lex_string(lexer, start, line, column);

    if (c == '\'')
        return lex_char(lexer, start, line, column);

    TokenKind kind = lex_operator(lexer, c);

    if (kind != TOKEN_INVALID)
        return make_token(lexer, kind, start, line, column);

    advance_char(lexer);

    lexer_error(lexer, LEXER_ERROR_INVALID_CHARACTER, start, line, column, "invalid character");

    return make_token(lexer, TOKEN_INVALID, start, line, column);
}
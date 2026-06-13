#include <ctype.h>
#include <stdio.h>
#include "lexer.h"
#include "error.h"
#include "optional.h"
#include "string.h"

INSTANTIATE(char, char, OPTIONAL_TEMPLATE)

static char_optional peek(Lexer *ctx) {
    if (ctx->source.index >= ctx->source.contents.length) {
        return (char_optional){false};
    }
    return (char_optional){true, ctx->source.contents.data[ctx->source.index]};
}

static char consume(Lexer *ctx) {
    char c = string_at(ctx->source.contents, ctx->source.index++);
    if (c == '\n') {
        ctx->line++;
        ctx->col = 1;
    } else {
        ctx->col++;
    }

    return c;
}

typedef struct {
    char *name;
    TokenType type;
} Keyword;

static Keyword keywords[] = {
    {"module", TOKENTYPE_MODULE},
    {"include", TOKENTYPE_INCLUDE},
    {"fn", TOKENTYPE_FN},
    {"return", TOKENTYPE_RETURN},
    {"struct", TOKENTYPE_STRUCT},
    {"union", TOKENTYPE_UNION},
    {"defer", TOKENTYPE_DEFER},
    {"match", TOKENTYPE_MATCH},
    {"enum", TOKENTYPE_ENUM},
    {"type", TOKENTYPE_TYPE},
    {"mut", TOKENTYPE_MUT},
    {"if", TOKENTYPE_IF},
    {"else", TOKENTYPE_ELSE},
    {"for", TOKENTYPE_FOR},
    {"while", TOKENTYPE_WHILE},
    {"break", TOKENTYPE_BREAK},
    {"continue", TOKENTYPE_CONTINUE},
    {"true", TOKENTYPE_TRUE},
    {"false", TOKENTYPE_FALSE},
    {"null", TOKENTYPE_NULL}
};

static Token decode_ident(Lexer *ctx, char_array *buf) {
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        Keyword *k = keywords + i;
        if (strcmp(buf->data, k->name) == 0) {
            return (Token){ .type = k->type };
        }
    }

    char *type_name = arena_strdup(ctx->arena, buf->data);
    String name_str = { .data = type_name, .length = strlen(type_name) };
    return (Token){ .type = TOKENTYPE_IDENT, .value = (string_optional){true, name_str} };
}

static char decode_esc(Lexer *ctx) {
    if (peek(ctx).has_value && peek(ctx).value == '\\') {
        consume(ctx);
        if (!peek(ctx).has_value) return 0xFF;
        char c = consume(ctx);
        switch (c) {
            case 'n': return '\n';
            case 't': return '\t';
            case 'r': return '\r';
            case 'b': return '\b';
            case '"': return '\"';
            case '\'': return '\'';
            case '\\': return '\\';
            default: return c;
        }
    } else {
        return consume(ctx);
    }
}

Token lex_next_token(Lexer *ctx) {
    if (!ctx || !ctx->source.contents.data) {
        return (Token){ .type = TOKENTYPE_EOF };
    }

    
    char_array buffer = char_array_init();
    
    while (true) {
        char_optional p = peek(ctx);
        
        if (!p.has_value) {
            return (Token){ .type = TOKENTYPE_EOF, .span = {ctx->source.index, 0}, .line = ctx->line, .col = ctx->col };
        }
        
        if (isspace((unsigned char)p.value)) {
            consume(ctx);
            continue;
        }
        
        size_t start_col = ctx->col, start_line = ctx->line;
        size_t start = ctx->source.index;

        if (isalpha((unsigned char)p.value)) {
            char_array_push(&buffer, consume(ctx));
            p = peek(ctx);
            while (p.has_value && (isalnum((unsigned char)p.value) || p.value == '_')) {
                char_array_push(&buffer, consume(ctx));
                p = peek(ctx);
            }
            char_array_push(&buffer, '\0');

            Token t = decode_ident(ctx, &buffer);
            t.span = (Span){ .start = start, .length = ctx->source.index - start };
            t.source = &ctx->source;
            t.line = start_line;
            t.col = start_col;
            return t;
        }

        if (isdigit((unsigned char)p.value)) {
            char_array_push(&buffer, consume(ctx));
            p = peek(ctx);
            while (p.has_value && isdigit((unsigned char)p.value)) {
                char_array_push(&buffer, consume(ctx));
                p = peek(ctx);
            }

            bool is_unsigned = false;
            if (peek(ctx).has_value && (peek(ctx).value == 'u' || peek(ctx).value == 'U')) {
                consume(ctx);
                is_unsigned = true;
            }
            char_array_push(&buffer, '\0');

            char *num_str = arena_strdup(ctx->arena, buffer.data);
            String num_string = { .data = num_str, .length = strlen(num_str) };
            
            Token t = { 
                .type = is_unsigned ? TOKENTYPE_UINT_LIT : TOKENTYPE_INT_LIT, 
                .value = (string_optional){true, num_string} 
            };
            t.span = (Span){ .start = start, .length = ctx->source.index - start };
            t.source = &ctx->source;
            t.line = start_line;
            t.col = start_col;
            return t;
        }

        char c = consume(ctx);
        Token t = { .line = start_line, .col = start_col };

        switch (c) {
            case '@': t.type = TOKENTYPE_AT; break;
            case '$': t.type = TOKENTYPE_DOLLAR; break;
            case '"': {
                while (peek(ctx).has_value && peek(ctx).value != '"') {
                    char_array_push(&buffer, decode_esc(ctx));
                }
                char_array_push(&buffer, '\0');
                consume(ctx);
                char *str = arena_strdup(ctx->arena, buffer.data);
                String str_string = { .data = str, .length = strlen(str) };
                t.type = TOKENTYPE_STR_LIT;
                t.value = (string_optional){true, str_string};
                char_array_clear(&buffer);
                break;
            }
            case '\'': {
                char esc_c = decode_esc(ctx);
                if (!peek(ctx).has_value || peek(ctx).value != '\'') {
                    t.span = (Span){.start = start, .length = ctx->source.index - start};
                    error(t, "Unterminated character literal");
                    exit(1);
                }
                consume(ctx);
                char *char_str = arena_strdup(ctx->arena, (char[]){esc_c, '\0'});
                String char_string = { .data = char_str, .length = 1 };
                t.type = TOKENTYPE_CHAR_LIT;
                t.value = (string_optional){true, char_string};
                break;
            }
            case '/': {
                if (peek(ctx).has_value && peek(ctx).value == '/') {
                    consume(ctx);
                    while (peek(ctx).has_value && peek(ctx).value != '\n') {
                        consume(ctx);
                    }
                    continue;
                } else if (peek(ctx).has_value && peek(ctx).value == '=') {
                    consume(ctx);
                    t.type = TOKENTYPE_SLASHEQ;
                } else {
                    t.type = TOKENTYPE_SLASH;
                }
                break;
            }
            case ':':
                if (peek(ctx).has_value && peek(ctx).value == ':') { consume(ctx); t.type = TOKENTYPE_DOUBLECOLON; } 
                else t.type = TOKENTYPE_COLON;
                break;
            case '(': t.type = TOKENTYPE_LPAREN; break;
            case ')': t.type = TOKENTYPE_RPAREN; break;
            case '{': t.type = TOKENTYPE_LBRACE; break;
            case '}': t.type = TOKENTYPE_RBRACE; break;
            case '[': t.type = TOKENTYPE_LBRACK; break;
            case ']': t.type = TOKENTYPE_RBRACK; break;
            case ';': t.type = TOKENTYPE_SEMICOLON; break;
            case '&': t.type = TOKENTYPE_AMP; break;
            case '%': t.type = TOKENTYPE_PERCENT; break;
            case '+':
                if (peek(ctx).has_value && peek(ctx).value == '=') { consume(ctx); t.type = TOKENTYPE_PLUSEQ; }
                else t.type = TOKENTYPE_PLUS;
                break;
            case '!':
                if (peek(ctx).has_value && peek(ctx).value == '=') { consume(ctx); t.type = TOKENTYPE_NEQ; }
                else t.type = TOKENTYPE_NOT;
                break;
            case '-':
                if (peek(ctx).has_value && peek(ctx).value == '=') { consume(ctx); t.type = TOKENTYPE_MINUSEQ; }
                else if (peek(ctx).has_value && peek(ctx).value == '>') { consume(ctx); t.type = TOKENTYPE_RARROW; }
                else t.type = TOKENTYPE_MINUS;
                break;
            case '*':
                if (peek(ctx).has_value && peek(ctx).value == '=') { consume(ctx); t.type = TOKENTYPE_STAREQ; }
                else t.type = TOKENTYPE_STAR;
                break;
            case '<':
                if (peek(ctx).has_value && peek(ctx).value == '<') { consume(ctx); if (peek(ctx).has_value && peek(ctx).value == '=') { consume(ctx); t.type = TOKENTYPE_SHLEQ; } else t.type = TOKENTYPE_SHL; }
                else if (peek(ctx).has_value && peek(ctx).value == '=') { consume(ctx); t.type = TOKENTYPE_LE; }
                else t.type = TOKENTYPE_LT;
                break;
            case '>':
                if (peek(ctx).has_value && peek(ctx).value == '>') { consume(ctx); if (peek(ctx).has_value && peek(ctx).value == '=') { consume(ctx); t.type = TOKENTYPE_SHREQ; } else t.type = TOKENTYPE_SHR; }
                else if (peek(ctx).has_value && peek(ctx).value == '=') { consume(ctx); t.type = TOKENTYPE_GE; }
                else t.type = TOKENTYPE_GT;
                break;
            case '=':
                if (peek(ctx).has_value && peek(ctx).value == '=') { consume(ctx); t.type = TOKENTYPE_EQEQ; }
                else t.type = TOKENTYPE_EQ;
                break;
            case ',': t.type = TOKENTYPE_COMMA; break;
            case '.': t.type = TOKENTYPE_DOT; break;
            case '?': t.type = TOKENTYPE_QUESTION; break;
            case '|': t.type = TOKENTYPE_PIPE; break;
            default:
                printf("Unknown character %c\n", c);
                exit(1);
        }

        t.span = (Span){ .start = start, .length = ctx->source.index - start };
        t.source = &ctx->source;
        return t;
    }
}
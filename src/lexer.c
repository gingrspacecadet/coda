#include <ctype.h>
#include <stdio.h>
#include "lexer.h"
#include "optional.h"
#include "string.h"

INSTANTIATE(char, char, ARRAY_TEMPLATE)
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
            return (Token){ .type = k->type, .value = arena_strdup(ctx->arena, buf->data) };
        }
    }

    return (Token){ .type = TOKENTYPE_IDENT, .value = arena_strdup(ctx->arena, buf->data) };
}

static char decode_esc(Lexer *ctx) {
    if (peek(ctx).has_value && peek(ctx).value == '\\') {
        consume(ctx);
        if (!peek(ctx).has_value) return 0xFF;
        char c = peek(ctx).value;
        consume(ctx);

        if (!peek(ctx).has_value) return 0xFF;
        switch (peek(ctx).value) {
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

token_array lex(Lexer *ctx) {
    if (!ctx) return token_array_empty;
    if (!ctx->source.contents.data) return token_array_empty;
    if (!ctx->arena) return token_array_empty;

    token_array tokens = {};
    char_array buffer = {};

    char_optional p = peek(ctx);

    while (p.has_value) {
        // putc(c.value, stderr);
        char_array_clear(&buffer);

        size_t start = ctx->source.index;

        bool not_pushed = false;

        if (isspace((unsigned char)p.value)) {
            consume(ctx);
            p = peek(ctx);
            while (p.has_value && isspace((unsigned char)p.value)) {
                consume(ctx);
                p = peek(ctx);
            }
            not_pushed = true;
        }
        
        if (isalpha((unsigned char)p.value)) {
            char_array_push(&buffer, consume(ctx));
            p = peek(ctx);
            while (p.has_value && (isalpha((unsigned char)p.value) || p.value == '_')) {
                char_array_push(&buffer, consume(ctx));
                p = peek(ctx);
            }

            token_array_push(&tokens, decode_ident(ctx, &buffer));
        }
        else if (isdigit((unsigned char)p.value)) {
            char_array_push(&buffer, consume(ctx));
            p = peek(ctx);
            while (p.has_value && isdigit((unsigned char)p.value)) {
                char_array_push(&buffer, consume(ctx));
                p = peek(ctx);
            }

            token_array_push(&tokens, (Token){ .type = TOKENTYPE_INT_LIT, .value = arena_strdup(ctx->arena, buffer.data)});
        }
        else {
            char c = consume(ctx);
            switch (c) {
                case '@': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_AT });
                    break;
                }
                case '\'': {
                    char c = decode_esc(ctx);
                    if (!peek(ctx).value || peek(ctx).value != '\'') {
                        printf("Unterminated character literal\n"); //TODO: nice errors
                        exit(1);
                    }
                    consume(ctx);
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_CHAR_LIT, .value = arena_strdup(ctx->arena, (char[]){c, '\0'})});
                    break;
                }
                case '"': {
                    while (peek(ctx).has_value && peek(ctx).value != '"') {
                        char_array_push(&buffer, decode_esc(ctx));
                    }
                    consume(ctx);
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_STR_LIT, .value = arena_strdup(ctx->arena, buffer.data)});
                    char_array_clear(&buffer);
                    break;
                }
                case '/': {
                    if (peek(ctx).has_value && peek(ctx).value == '/') {
                        consume(ctx);
                        while (peek(ctx).has_value && peek(ctx).value != '\n') {
                            consume(ctx);
                            not_pushed = true;
                        }
                    } else {
                        if (peek(ctx).has_value && peek(ctx).value == '=') {
                            consume(ctx);
                            token_array_push(&tokens, (Token){ .type = TOKENTYPE_SLASHEQ });
                        } else {
                            token_array_push(&tokens, (Token){ .type = TOKENTYPE_SLASH });
                        }
                    }
                    break;
                }
                case ':': {
                    if (peek(ctx).has_value && peek(ctx).value == ':') {
                        consume(ctx);
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_DOUBLECOLON });
                    } else {
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_COLON });
                    }
                    break;
                }
                case '(': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_LPAREN });
                    break;
                }
                case ')': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_RPAREN });
                    break;
                }
                case '{': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_LBRACE });
                    break;
                }
                case '}': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_RBRACE });
                    break;
                }
                case '[': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_LBRACK });
                    break;
                }
                case ']': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_RBRACK });
                    break;
                }
                case ';': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_SEMICOLON });
                    break;
                }
                case '&': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_AMP });
                    break;
                }
                case '%': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_PERCENT });
                    break;
                }
                case '+': {
                    if (peek(ctx).has_value && peek(ctx).value == '=') {
                        consume(ctx);
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_PLUSEQ });
                    } else {
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_PLUS });
                    }
                    break;
                }
                case '!': {
                    if (peek(ctx).has_value && peek(ctx).value == '=') {
                        consume(ctx);
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_NEQ });
                    } else {
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_NOT });
                    }
                    break;
                }
                case '-': {
                    if (peek(ctx).has_value && peek(ctx).value == '=') {
                        consume(ctx);
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_MINUSEQ });
                    } else {
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_MINUS });
                    }
                    break;
                }
                case '*': {
                    if (peek(ctx).has_value && peek(ctx).value == '=') {
                        consume(ctx);
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_STAREQ });
                    } else {
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_STAR });
                    }
                    break;
                }
                case '<': {
                    if (peek(ctx).has_value && peek(ctx).value == '<') {
                        consume(ctx);
                        if (peek(ctx).has_value && peek(ctx).value == '=') {
                            consume(ctx);
                            token_array_push(&tokens, (Token){ .type = TOKENTYPE_SHLEQ });
                        } else {
                            token_array_push(&tokens, (Token){ .type = TOKENTYPE_SHL });
                        }
                    } else if (peek(ctx).has_value && peek(ctx).value == '=') {
                        consume(ctx);
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_LE });
                    } else {
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_LT });
                    }
                    break;
                }
                case '>': {
                    if (peek(ctx).has_value && peek(ctx).value == '>') {
                        consume(ctx);
                        if (peek(ctx).has_value && peek(ctx).value == '=') {
                            consume(ctx);
                            token_array_push(&tokens, (Token){ .type = TOKENTYPE_SHREQ });
                        } else {
                            token_array_push(&tokens, (Token){ .type = TOKENTYPE_SHR });
                        }
                    } else if (peek(ctx).has_value && peek(ctx).value == '=') {
                        consume(ctx);
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_GE });
                    } else {
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_GT });
                    }
                    break;
                }
                case '=': {
                    if (peek(ctx).has_value && peek(ctx).value == '=') {
                        consume(ctx);
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_EQEQ });
                    } else {
                        token_array_push(&tokens, (Token){ .type = TOKENTYPE_EQ });
                    }
                    break;
                }
                case ',': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_COMMA });
                    break;
                }
                case '.': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_DOT });
                    break;
                }
                case '?': {
                    token_array_push(&tokens, (Token){ .type = TOKENTYPE_QUESTION });
                    break;
                }
                default: {
                    printf("Unknown character 0x%02X\n", c);
                    exit(1);
                }
            }
        }

        if (!not_pushed) {
            Token *last = tokens.data + tokens.idx - 1;
            last->span = (Span){ .start = start, .length = ctx->source.index - start };
            last->line = ctx->line;
            last->col = ctx->col;
        }

        p = peek(ctx);
    }

    ctx->source.index = 0;
    return tokens;
}
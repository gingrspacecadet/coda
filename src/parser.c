#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "log.h"
#include "parser.h"

static unsigned int line = 0, col = 0;

#define token_add(token) do {\
    if (num_tokens == capacity) {\
        capacity += 1024;\
        tokens = realloc(tokens, (num_tokens + capacity) * sizeof(Token));\
    }\
    tokens[num_tokens++] = token;\
} while (0)

#define peek(src, offset) (src[offset])

size_t parse(char *src, Token **out) {
    size_t capacity = 1024;
    Token *tokens = malloc(capacity * sizeof(Token));
    size_t num_tokens = 0;

    while (*src) {
        if (peek(src, 0 == '\n')) {line++; col = 0; src++; continue;}
        if (isspace(*src)) { col++; src++; continue; }

        if (peek(src, 0) == '/') {
            src++;
            if (peek(src, 0) == '/') {
                while (*++src != '\n');
                line++; col = 0; src++; continue;
            }
            if (peek(src, 0) == '*') {
                while (*src != '*' && src[1] != '/') {
                    col++;
                    if (*src == '\n') line++; col = 0;
                    src++;
                }
            }
            src--;
        }

        if (peek(src, 0) == 'a' && peek(src, 1) == 's' && peek(src, 2) == 'm') {
            while ()
            src += 5;
            size_t len = 0;
            Token t = (Token){ .type = ASM, .data = src, };
            while (*src++ != '}') len++;
            t.len = len;
            token_add(t);
            src++; continue;
        }

        src++;
    }

    *out = tokens;
    return num_tokens;
}

/*
IDENT: [A-Za-z_][A-Za-z0-9_]*
ATTR: @IDENT
KEYWORD: match on name
NUMBERS: 
decimal: [0-9]*
hex: 0x[0-9A-Fa-f]*
binary: 0b[01]*
STRINGS: "*"
CHAR: '*'
COMMENDS: // or /*
PUNCTUATION: longest match
ASM: just read everything within the {}
*/
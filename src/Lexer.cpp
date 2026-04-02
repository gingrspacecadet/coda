#include <sstream>
#include <fstream>
#include <cctype>
#include <iostream>
#include "Lexer.hpp"

Lexer::Lexer(const std::string& path): m_Path(path) {
    std::stringstream contents_stream;

    std::fstream input(path, std::ios::in);
    contents_stream << input.rdbuf();
    m_FileContents = contents_stream.str();
}

struct Keyword {
    const std::string& name;
    TokenType type;
};

static Keyword keywords[] = {
    {"module", TokenType::MODULE},
    {"include", TokenType::INCLUDE},
    {"fn", TokenType::FN},
    {"return", TokenType::RETURN},
    {"struct", TokenType::STRUCT},
    {"union", TokenType::UNION},
    {"enum", TokenType::ENUM},
    {"type", TokenType::TYPE},
    {"mut", TokenType::MUT},
    {"if", TokenType::IF},
    {"else", TokenType::ELSE},
    {"for", TokenType::FOR},
    {"while", TokenType::WHILE},
    {"break", TokenType::BREAK},
    {"continue", TokenType::CONTINUE},
    {"true", TokenType::TRUE},
    {"false", TokenType::FALSE},
    {"null", TokenType::_NULL}
};

Token Lexer::DecodeIdent(std::string& buf) {
    for (const auto& k : keywords) {
        if (buf == k.name) {
            return (Token){.type = k.type};
        }
    }

    return (Token){.type = TokenType::IDENT, .value = buf};
}

char Lexer::DecodeEsc() {
    if (peek().has_value() && peek().value() == '\\') {
        consume();
        if (!peek().has_value()) return EOF;
        char c = peek().value();
        consume();

        if (!peek().has_value()) return EOF;
        switch (peek().value()) {
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
        return consume();
    }
}

std::vector<Token> Lexer::Lex() {
    std::vector<Token> tokens;
    std::string buf;

    while (peek().has_value()) {
        buf.clear();

        if (std::isspace(peek().value())) {
            consume();
        }
        else if (std::isalpha(peek().value())) {
            buf.push_back(consume());
            while (peek().has_value() && (std::isalnum(peek().value()) || peek().value() == '_')) {
                buf.push_back(consume());
            }

            tokens.push_back(DecodeIdent(buf));
        }
        else if (std::isdigit(peek().value())) {
            buf.push_back(consume());
            while (peek().has_value() && std::isdigit(peek().value())) {
                buf.push_back(consume());
            }

            tokens.push_back((Token){.type = TokenType::INT_LIT, .value = buf});
        }
        else {
            switch (peek().value()) {
                case '@': {
                    consume();

                    tokens.push_back((Token){.type = TokenType::AT});
                    break;
                }
                case '\'': {
                    consume();
                    char c = DecodeEsc();
                    if (!peek().has_value() || peek().value() != '\'') {
                        std::cerr << "Unterminated character literal" << std::endl;
                        exit(1);
                    }
                    consume();
                    char tmp[2] = {c, '\0'};
                    std::string str{tmp};
                    tokens.push_back((Token){.type = TokenType::CHAR_LIT, .value = str});
                    break;
                }
                case '"': {
                    consume();
                    while (peek().has_value() && peek().value() != '"') {
                        buf.push_back(DecodeEsc());
                    }
                    consume();
                    tokens.push_back((Token){.type = TokenType::STR_LIT, .value = buf});
                    buf.clear();
                    break;
                }
                case '/': {
                    consume();
                    if (peek().has_value() && peek().value() == '/') {
                        consume();
                        while (peek().has_value() && peek().value() != '\n') {
                            consume();
                        }
                    } else {
                        if (peek().has_value() && peek().value() == '=') {
                            consume();
                            tokens.push_back((Token){.type = TokenType::SLASHEQ});
                        } else {
                            tokens.push_back((Token){.type = TokenType::SLASH});
                        }
                    }
                    break;
                }
                case ':': {
                    consume();
                    if (peek().has_value() && peek().value() == ':') {
                        consume();
                        tokens.push_back((Token){.type = TokenType::DOUBLECOLON});
                    } else {
                        tokens.push_back((Token){.type = TokenType::COLON});
                    }
                    break;
                }
                case '(': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::LPAREN});
                    break;
                }
                case ')': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::RPAREN});
                    break;
                }
                case '{': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::LBRACE});
                    break;
                }
                case '}': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::RBRACE});
                    break;
                }
                case '[': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::LBRACK});
                    break;
                }
                case ']': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::RBRACK});
                    break;
                }
                case ';': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::SEMICOLON});
                    break;
                }
                case '&': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::AMP});
                    break;
                }
                case '%': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::PERCENT});
                    break;
                }
                case '+': {
                    consume();
                    if (peek().has_value() && peek().value() == '=') {
                        consume();
                        tokens.push_back((Token){.type = TokenType::PLUSEQ});
                    } else {
                        tokens.push_back((Token){.type = TokenType::PLUS});
                    }
                    break;
                }
                case '!': {
                    consume();
                    if (peek().has_value() && peek().value() == '=') {
                        consume();
                        tokens.push_back((Token){.type = TokenType::NEQ});
                    } else {
                        tokens.push_back((Token){.type = TokenType::NOT});
                    }
                    break;
                }
                case '-': {
                    consume();
                    if (peek().has_value() && peek().value() == '=') {
                        consume();
                        tokens.push_back((Token){.type = TokenType::MINUSEQ});
                    } else {
                        tokens.push_back((Token){.type = TokenType::MINUS});
                    }
                    break;
                }
                case '*': {
                    consume();
                    if (peek().has_value() && peek().value() == '=') {
                        consume();
                        tokens.push_back((Token){.type = TokenType::STAREQ});
                    } else {
                        tokens.push_back((Token){.type = TokenType::STAR});
                    }
                    break;
                }
                case '<': {
                    consume();
                    if (peek().has_value() && peek().value() == '<') {
                        consume();
                        if (peek().has_value() && peek().value() == '=') {
                            consume();
                            tokens.push_back((Token){.type = TokenType::SHLEQ});
                        } else {
                            tokens.push_back((Token){.type = TokenType::SHL});
                        }
                    } else if (peek().has_value() && peek().value() == '=') {
                        consume();
                        tokens.push_back((Token){.type = TokenType::LE});
                    } else {
                        tokens.push_back((Token){.type = TokenType::LT});
                    }
                    break;
                }
                case '>': {
                    consume();
                    if (peek().has_value() && peek().value() == '>') {
                        consume();
                        if (peek().has_value() && peek().value() == '=') {
                            consume();
                            tokens.push_back((Token){.type = TokenType::SHREQ});
                        } else {
                            tokens.push_back((Token){.type = TokenType::SHR});
                        }
                    } else if (peek().has_value() && peek().value() == '=') {
                        consume();
                        tokens.push_back((Token){.type = TokenType::GE});
                    } else {
                        tokens.push_back((Token){.type = TokenType::GT});
                    }
                    break;
                }
                case '=': {
                    consume();
                    if (peek().has_value() && peek().value() == '=') {
                        consume();
                        tokens.push_back((Token){.type = TokenType::EQEQ});
                    } else {
                        tokens.push_back((Token){.type = TokenType::EQ});
                    }
                    break;
                }
                case ',': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::COMMA});
                    break;
                }
                case '.': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::DOT});
                    break;
                }
                case '?': {
                    consume();
                    tokens.push_back((Token){.type = TokenType::QUESTION});
                    break;
                }
                default: {
                    std::cerr << "Unknown character '" << peek().value() << "'" << std::endl;
                    exit(1);
                }
            }
        }
    }

    m_Index = 0;
    return tokens;
}
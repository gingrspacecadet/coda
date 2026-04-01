#include <sstream>
#include <fstream>
#include <cctype>
#include <iostream>
#include "Lexer.hpp"

namespace Coda {
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
    };

    static Token DecodeIdent(std::string& buf) {
        for (auto& k : keywords) {
            if (k.name == buf) {
                return (Token){.type = k.type};
            }
        }

        return (Token){.type = TokenType::IDENT, .value = buf};
    }

    std::vector<Token> Lexer::Lex() {
        std::vector<Token> tokens;
        std::string buf;

        while (peek().has_value()) {
            if (std::isspace(peek().value())) {
                while (peek().has_value() && std::isspace(peek().value())) {
                    consume();
                }
            }
            
            if (std::isalpha(peek().value())) {
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

                tokens.push_back((Token){.type = TokenType::NUMBER, .value = buf});
            }
            else {
                switch (peek().value()) {
                    case ';': consume(); tokens.push_back((Token){.type = TokenType::SEMI}); break;
                    default: {
                        std::cerr << "Unknown character '" << peek().value() << "'" << std::endl;
                        exit(1);
                    }
                }
            }
        }

        return tokens;
    }
}
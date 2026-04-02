#pragma once

#include <string>
#include <optional>
#include <vector>

enum class TokenType {
    MODULE,
    INCLUDE,
    FN,
    RETURN,
    STRUCT,
    UNION,
    ENUM,
    IDENT,
    AT,
    MUT,
    IF,
    ELSE,
    FOR,
    WHILE,
    BREAK,
    CONTINUE,
    STR_LIT,
    CHAR_LIT,
    INT_LIT,    // TODO: more number literals maybe
    TRUE,
    FALSE,
    _NULL,
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    LBRACK,
    RBRACK,
    COLON,
    SEMICOLON,
    DOUBLECOLON,
    COMMA,
    DOT,
    AMP,
    QUESTION,
    NOT,
    CARET,
    PERCENT,
    PIPE,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    SHR,
    SHL,
    GT,
    LT,
    EQEQ,
    NEQ,
    GE,
    LE,
    AMPAMP,
    PIPEPIPE,
    EQ,
    PLUSEQ,
    MINUSEQ,
    STAREQ,
    SLASHEQ,
    SHREQ,
    SHLEQ,
    _EOF,
};

struct Token {
    TokenType type;
    std::optional<std::string> value {};
};

class Lexer {
public:
    Lexer(const std::string& path);

    std::vector<Token> Lex();

private:
    const std::string& m_Path; 
    std::string m_FileContents;
    size_t m_Index = 0;

    [[nodiscard]] std::optional<char> peek(const size_t offset = 0) const {
        if (m_Index + offset >= m_FileContents.length()) {
            return {};
        }
        return m_FileContents.at(m_Index + offset);
    }
    
    char consume() {
        return m_FileContents.at(m_Index++);
    }

    inline Token DecodeIdent(std::string& buf);
    inline char DecodeEsc();
};

// inline std::ostream& operator<<(std::ostream& os, const Token& t) {        
//     switch (t.type) {
//         case TokenType::AMP: return os << "&";
//         case TokenType::AMPAMP: return os << "&&";
//         case TokenType::ATTR: return os << "@" << t.value.value();
//         case TokenType::BREAK: return os << "break";
//         case TokenType::CARET: return os << "^";
//         case TokenType::CHAR_LIT: return os << "'" << t.value.value() << "'";
//         case TokenType::COLON: return os << ":";
//         case TokenType::COMMA: return os << ",";
//         case TokenType::CONTINUE: return os << "continue";
//         case TokenType::DOT: return os << ".";
//         case TokenType::DOUBLECOLON: return os << "::";
//         case TokenType::ELSE: return os << "else";
//         case TokenType::EQ: return os << "=";
//         case TokenType::EQEQ: return os << "==";
//         case TokenType::FALSE: return os << "false";
//         case TokenType::FN: return os << "fn";
//         case TokenType::FOR: return os << "for";
//         case TokenType::GE: return os << ">=";
//         case TokenType::GT: return os << ">";
//         case TokenType::IDENT: return os << t.value.value();
//         case TokenType::IF: return os << "if";
//         case TokenType::INCLUDE: return os << "include";
//         case TokenType::INT_LIT: return os << t.value.value();
//         case TokenType::LBRACE: return os << "{";
//         case TokenType::LBRACK: return os << "[";
//         case TokenType::LE: return os << "<=";
//         case TokenType::LPAREN: return os << "(";
//         case TokenType::LT: return os << "<";
//         case TokenType::MINUS: return os << '-';
//         case TokenType::MINUSEQ: return os << "-=";
//         case TokenType::MODULE: return os << "module";
//         case TokenType::MUT: return os << "mut";
//         case TokenType::NEQ: return os << "!=";
//         case TokenType::NOT: return os << "!";
//         case TokenType::PIPE: return os << "|";
//         case TokenType::PIPEPIPE: return os << "||";
//         case TokenType::PLUS: return os << "+";
//         case TokenType::PLUSEQ: return os << "+=";
//         case TokenType::QUESTION: return os << "?";
//         case TokenType::RBRACE: return os << "}";
//         case TokenType::RBRACK: return os << "]";
//         case TokenType::RETURN: return os << "return";
//         case TokenType::RPAREN: return os << ")";
//         case TokenType::SEMICOLON: return os << ";";
//         case TokenType::SHL: return os << "<<";
//         case TokenType::SHLEQ: return os << "<<=";
//         case TokenType::SHR: return os << ">>";
//         case TokenType::SHREQ: return os << ">>=";
//         case TokenType::SLASH: return os << "/";
//         case TokenType::SLASHEQ: return os << "/=";
//         case TokenType::STAR: return os << "*";
//         case TokenType::STAREQ: return os << "*=";
//         case TokenType::STR_LIT: return os << "\"" << t.value.value() << "\"";
//         case TokenType::TRUE: return os << "true";
//         case TokenType::WHILE: return os << "while";
//         case TokenType::_EOF: return os << "EOF";
//         case TokenType::_NULL: return os << "null";
//         default: return os << "<unknown>";
//     }
// }
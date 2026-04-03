#pragma once

enum class TokenType {
    MODULE,
    INCLUDE,
    FN,
    RETURN,
    STRUCT,
    UNION,
    ENUM,
    TYPE,
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

struct Span {
    size_t start, length;
};

struct Token {
    TokenType type;
    std::optional<std::string> value {};
    Span span;
    size_t line, col;
};

class Lexer {
public:
    // some C++ magic that prevents accidental moves/copies
    explicit Lexer(const std::string& path);
    Lexer(const Lexer&) = delete;
    Lexer& operator=(const Lexer&) = delete;
    Lexer(Lexer&&) = delete;
    Lexer& operator=(Lexer&&) = delete;

    std::vector<Token> Lex();

    const std::string& GetPath() const { return m_Path; }
    const std::string& GetFileContents() const { return m_FileContents; }

private:
    const std::string& m_Path; 
    std::string m_FileContents;
    size_t m_Index = 0;

    size_t m_LineNum = 1, m_ColNum = 1;

    [[nodiscard]] std::optional<char> peek(const size_t offset = 0) const {
        if (m_Index + offset >= m_FileContents.length()) {
            return {};
        }
        return m_FileContents.at(m_Index + offset);
    }
    
    char consume() {
        char c = m_FileContents.at(m_Index++);
        if (c == '\n') {
            m_LineNum++;
            m_ColNum = 1;
        } else {
            m_ColNum++;
        }

        return c;
    }

    inline Token DecodeIdent(std::string& buf);
    inline char DecodeEsc();
};

inline std::string_view TokenTypeToString(const TokenType& type) {
    switch (type) {
        case TokenType::AMP: return "AMP";
        case TokenType::AMPAMP: return "AMPAMP";
        case TokenType::AT: return "AT";
        case TokenType::BREAK: return "BREAK";
        case TokenType::CARET: return "CARET";
        case TokenType::CHAR_LIT: return "CHAR_LIT";
        case TokenType::COLON: return "COLON";
        case TokenType::COMMA: return "COMMA";
        case TokenType::CONTINUE: return "CONTINUE";
        case TokenType::DOT: return "DOT";
        case TokenType::DOUBLECOLON: return "DOUBLECOLON";
        case TokenType::ELSE: return "ELSE";
        case TokenType::EQ: return "EQ";
        case TokenType::EQEQ: return "EQEQ";
        case TokenType::FALSE: return "FALSE";
        case TokenType::FN: return "FN";
        case TokenType::FOR: return "FOR";
        case TokenType::GE: return "GE";
        case TokenType::GT: return "GT";
        case TokenType::IDENT: return "IDENT";
        case TokenType::IF: return "IF";
        case TokenType::INCLUDE: return "INCLUDE";
        case TokenType::INT_LIT: return "INT_LIT";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::LBRACK: return "LBRACK";
        case TokenType::LE: return "LE";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::LT: return "LT";
        case TokenType::MINUS: return "MINUS";
        case TokenType::MINUSEQ: return "MINUSEQ";
        case TokenType::MODULE: return "MODULE";
        case TokenType::MUT: return "MUT";
        case TokenType::NEQ: return "NEQ";
        case TokenType::NOT: return "NOT";
        case TokenType::PIPE: return "PIPE";
        case TokenType::PIPEPIPE: return "PIPEPIPE";
        case TokenType::PLUS: return "PLUS";
        case TokenType::PLUSEQ: return "PLUSEQ";
        case TokenType::QUESTION: return "QUESTION";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::RBRACK: return "RBRACK";
        case TokenType::RETURN: return "RETURN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::SHL: return "SHL";
        case TokenType::SHLEQ: return "SHLEQ";
        case TokenType::SHR: return "SHR";
        case TokenType::SHREQ: return "SHREQ";
        case TokenType::SLASH: return "SLASH";
        case TokenType::SLASHEQ: return "SLASHEQ";
        case TokenType::STAR: return "STAR";
        case TokenType::STAREQ: return "STAREQ";
        case TokenType::STR_LIT: return "STR_LIT";
        case TokenType::TRUE: return "TRUE";
        case TokenType::WHILE: return "WHILE";
        case TokenType::_EOF: return "EOF";
        case TokenType::_NULL: return "NULL";
        default: return "<unknown>";
    }
}

inline std::ostream& operator<<(std::ostream& os, const TokenType& t) {        
    return os << TokenTypeToString(t);
}

inline std::ostream& operator<<(std::ostream& os, const Token& t) {        
    switch (t.type) {
        case TokenType::AMP: return os << "&";
        case TokenType::AMPAMP: return os << "&&";
        case TokenType::AT: return os << "@";
        case TokenType::BREAK: return os << "break";
        case TokenType::CARET: return os << "^";
        case TokenType::CHAR_LIT: return os << "'" << t.value.value() << "'";
        case TokenType::COLON: return os << ":";
        case TokenType::COMMA: return os << ",";
        case TokenType::CONTINUE: return os << "continue";
        case TokenType::DOT: return os << ".";
        case TokenType::DOUBLECOLON: return os << "::";
        case TokenType::ELSE: return os << "else";
        case TokenType::EQ: return os << "=";
        case TokenType::EQEQ: return os << "==";
        case TokenType::FALSE: return os << "false";
        case TokenType::FN: return os << "fn";
        case TokenType::FOR: return os << "for";
        case TokenType::GE: return os << ">=";
        case TokenType::GT: return os << ">";
        case TokenType::IDENT: return os << t.value.value();
        case TokenType::IF: return os << "if";
        case TokenType::INCLUDE: return os << "include";
        case TokenType::INT_LIT: return os << t.value.value();
        case TokenType::LBRACE: return os << "{";
        case TokenType::LBRACK: return os << "[";
        case TokenType::LE: return os << "<=";
        case TokenType::LPAREN: return os << "(";
        case TokenType::LT: return os << "<";
        case TokenType::MINUS: return os << '-';
        case TokenType::MINUSEQ: return os << "-=";
        case TokenType::MODULE: return os << "module";
        case TokenType::MUT: return os << "mut";
        case TokenType::NEQ: return os << "!=";
        case TokenType::NOT: return os << "!";
        case TokenType::PIPE: return os << "|";
        case TokenType::PIPEPIPE: return os << "||";
        case TokenType::PLUS: return os << "+";
        case TokenType::PLUSEQ: return os << "+=";
        case TokenType::QUESTION: return os << "?";
        case TokenType::RBRACE: return os << "}";
        case TokenType::RBRACK: return os << "]";
        case TokenType::RETURN: return os << "return";
        case TokenType::RPAREN: return os << ")";
        case TokenType::SEMICOLON: return os << ";";
        case TokenType::SHL: return os << "<<";
        case TokenType::SHLEQ: return os << "<<=";
        case TokenType::SHR: return os << ">>";
        case TokenType::SHREQ: return os << ">>=";
        case TokenType::SLASH: return os << "/";
        case TokenType::SLASHEQ: return os << "/=";
        case TokenType::STAR: return os << "*";
        case TokenType::STAREQ: return os << "*=";
        case TokenType::STR_LIT: return os << "\"" << t.value.value() << "\"";
        case TokenType::TRUE: return os << "true";
        case TokenType::WHILE: return os << "while";
        case TokenType::_EOF: return os << "EOF";
        case TokenType::_NULL: return os << "null";
        default: return os << "<unknown>";
    }
}
#pragma once

#include "Lexer.hpp"

struct Expr;
struct Stmt;
struct Decl;
struct VarDecl;
struct FnDecl;
struct StructDecl;
struct TypeRef;
struct Symbol;
struct Scope;
struct Module;
struct Include;

class MemoryArena {
public:
    explicit MemoryArena(size_t block_size = 1024  *64) : m_BlockSize(block_size) {
        alloc_block();
    }

    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;

    ~MemoryArena() {
        for (auto& cleanup : m_CleanupTasks) {
            cleanup();
        }
        for (void *block : m_Blocks) {
            std::free(block);
        }
    }

    template<typename T, typename... Args>
    T *alloc(Args&&... args) {
        size_t size = sizeof(T);
        size_t alignment = alignof(T);

        void *ptr = std::align(alignment, size, m_CurrentPos, m_RemainingSpace);

        if (!ptr) {
            alloc_block();
            ptr = std::align(alignment, size, m_CurrentPos, m_RemainingSpace);
        }

        m_CurrentPos = static_cast<char*>(m_CurrentPos) + size;
        m_RemainingSpace -= size;

        T *object = new (ptr) T(std::forward<Args>(args)...);

        if constexpr (!std::is_trivially_destructible_v<T>) {
            m_CleanupTasks.push_back([object]() { object->~T(); });
        }

        return object;
    }

private:
    void alloc_block() {
        void *block = std::malloc(m_BlockSize);
        if (!block) throw std::bad_alloc();
        m_Blocks.push_back(block);
        m_CurrentPos = block;
        m_RemainingSpace = m_BlockSize;
    }

    size_t m_BlockSize;
    std::vector<void*> m_Blocks;
    std::vector<std::function<void()>> m_CleanupTasks;
    void *m_CurrentPos = nullptr;
    size_t m_RemainingSpace = 0;
};

enum class UnaryOp  { NEG, NOT, DEREF, ADDR };
enum class BinaryOp { 
    MUL, DIV, MOD, ADD, SUB, SHL, SHR, 
    LT, LE, GT, GE, EQ, NE, 
    AND, XOR, OR, LOG_AND, LOG_OR, 
    ASSIGN, ADD_ASSIGN
};

enum class SymbolFlags : uint32_t {
    NONE   = 0,
    FN     = 1 << 0,
    VAR    = 1 << 1,
    TYPE   = 1 << 2,
    EXPORT = 1 << 3,
    EXTERN = 1 << 4,
    MUT    = 1 << 5,
};

struct Attribute {
    std::string name;
    std::vector<std::string> args;

    void Print(int indent);
};

struct TypeRef {
    struct Named     { std::string name; };
    struct Pointer   { TypeRef *pointee; };
    struct Array     { TypeRef *elem; };

    std::variant<Named, Pointer, Array> data;
    bool is_mutable = false;
    bool is_optional = false;
    Symbol *type_symbol = nullptr;

    TypeRef(std::variant<Named, Pointer, Array> data, bool mut = false)
        : data(std::move(data)), is_mutable(mut) {}

    void Print(int indent);
};

struct Literal {
    enum class Type { INT, FLOAT, STRING, BOOL, CHAR };
    Type type;
    std::string raw;
    std::variant<int64_t, double, std::string, bool, char> value;
};

struct Expr {
    struct Ident  { std::string name; };
    struct Path   { std::vector<std::string> components; };
    struct Unary  { UnaryOp op; Expr *operand; };
    struct Binary { BinaryOp op; Expr *left; Expr *right; };
    struct Call   { Expr *callee; std::vector<Expr*> args; };
    struct Index  { Expr *base; Expr *index; };
    struct Member { Expr *base; std::string member; };
    struct Cast   { TypeRef *to; Expr *expr; };

    std::variant<Literal, Ident, Path, Unary, Binary, Call, Index, Member, Cast> data;
    TypeRef *resolved_type = nullptr;
    Symbol *symbol = nullptr;
    bool is_constant = false;

    Expr(std::variant<Literal, Ident, Path, Unary, Binary, Call, Index, Member, Cast> data)
        : data(data) {}

    void Print(int indent);
};

struct Stmt {
    struct Block  { std::vector<Stmt*> stmts; };
    struct Return { Expr *value; };
    struct If     { Expr *cond; Stmt *then_branch; Stmt *else_branch; };
    struct For    { Stmt *init; Expr *cond; Expr *post; Stmt *body; };
    struct While  { Expr *cond; Stmt *body; };
    struct Unsafe { std::vector<Stmt*> stmts; };

    std::variant<VarDecl*, Expr*, Block, Return, If, For, While, Unsafe> data;
    Scope *scope = nullptr;

    Stmt(std::variant<VarDecl*, Expr*, Block, Return, If, For, While, Unsafe> data)
        : data(data) {}

    void Print(int indent);
};

struct Param {
    TypeRef *type = nullptr;
    std::string name;
    std::vector<Attribute> attributes;
    Symbol *symbol = nullptr;

    void Print(int indent);
};

struct VarDecl {
    TypeRef *type = nullptr;
    std::string name;
    Expr *init = nullptr;
    std::vector<Attribute> attributes;
    Symbol *symbol = nullptr;
    bool is_mutable = false;
    bool is_def_init = false;

    VarDecl(TypeRef *type, std::string& name)
        : type(type), name(name) {}

    void Print(int indent);
};

struct FnDecl {
    std::string name;
    TypeRef *ret_type = nullptr;
    std::vector<Param> params;
    Stmt *body = nullptr;
    std::vector<Attribute> attributes;
    Symbol *symbol = nullptr;
    Scope *local_scope = nullptr;
    bool is_export = false;

    void Print(int indent);
};

struct Decl {
    std::variant<FnDecl*, VarDecl*, StructDecl*, Include*, Attribute*> data;
    Symbol *symbol = nullptr;

    void Print(int indent);
};

struct StructDecl {
    std::string name;
    std::vector<Decl*> members;
    std::vector<Attribute> attributes;
    Symbol *symbol = nullptr;
    size_t size = 0;
    size_t align = 0;
    std::vector<size_t> field_offsets;
    bool is_export = false;

    void Print(int indent);
};

struct Include {
    std::vector<std::string> path;
    std::optional<std::string> alias;
    Module *resolved = nullptr;

    void Print(int indent);
};

struct Symbol {
    std::string name;
    Decl *decl = nullptr;
    TypeRef *type = nullptr;
    uint32_t flags = 0;
    Scope *defined_in = nullptr;
};

struct Scope {
    std::vector<Symbol*> symbols;
    Scope *parent = nullptr;
};

struct Module {
    std::string name;
    std::vector<Include*> includes;
    std::vector<Decl*> decls;
    Scope *scope = nullptr;

    void Print(int indent = 0);
};

class Parser {
public:
    Parser(const std::string& path)
        : m_Lexer(path) {
            m_Tokens = m_Lexer.Lex();
        }

    Module *ParseModule();
    Include *ParseInclude();
    Decl *ParseDecl();
    FnDecl *ParseFnDecl();
    StructDecl *ParseStructDecl();
    TypeRef *ParseType();
    Stmt *ParseBlockStmt();
    Stmt *ParseStmt();
    Stmt *ParseVarStmt();
    Expr *ParseExpr(int min_bp = 0);
    Expr *ParseExprPrefix();
    Expr *ExprNewLit(Token& t);
    Expr *ExprNewIdent(Token& t);
    Expr *ExprHandlePostfix(Expr *left);
    Stmt *ParseReturnStmt();
    Stmt *ParseForStmt();
    Stmt *ParseIfStmt();
    Stmt *ParseWhileStmt();
    Stmt *ParseExprStmt();
    void CollectAttributes(std::vector<Attribute>& out);

private:
    Lexer m_Lexer;
    std::vector<Token> m_Tokens;
    MemoryArena m_Arena;
    size_t m_Index = 0;

    template<typename T, typename... Args>
    T *make(Args&&... args) {
        return m_Arena.alloc<T>(std::forward<Args>(args)...);
    }

    [[nodiscard]] std::optional<Token> peek(const size_t offset = 0) const {
        if (m_Index + offset >= m_Tokens.size()) return std::nullopt;
        return m_Tokens.at(m_Index + offset);
    }

    Token consume() {
        return m_Tokens.at(m_Index++);
    }

    [[noreturn]] void error(const std::string& msg) {
        auto t = peek();
        constexpr const char* BOLD_WHITE = "\x1b[1;37m";
        constexpr const char* RED = "\x1b[1;31m";
        constexpr const char* RESET = "\x1b[0m";

        if (!t) {
            std::cout << BOLD_WHITE << m_Lexer.GetPath() << ":0:0: "
                    << RED << "error: " << RESET << msg << '\n';
            std::cout << RED << "error: " << RESET << "at end of file\n";
            std::exit(1);
        }

        std::string_view file_view(m_Lexer.GetFileContents());
        size_t span_start = t->span.start;
        size_t span_len = t->span.length;

        size_t line_start = span_start;
        while (line_start > 0 && file_view[line_start - 1] != '\n') --line_start;
        size_t line_end = span_start;
        while (line_end < file_view.size() && file_view[line_end] != '\n') ++line_end;

        std::string_view line_view = file_view.substr(line_start, line_end - line_start);

        size_t col = t->col ? t->col : (span_start - line_start + 1);

        std::string printable_line;
        printable_line.reserve(line_view.size());
        for (char c : line_view) {
            if (c == '\t') printable_line.append(4, ' ');
            else printable_line.push_back(c);
        }

        size_t caret_pos = 0;
        for (size_t i = 0, src_i = 0; i < printable_line.size() && src_i < (span_start - line_start); ++src_i) {
            if (file_view[line_start + src_i] == '\t') {
                caret_pos += 4;
            } else {
                ++caret_pos;
            }
        }

        size_t caret_len = 1;
        if (span_len > 0) {
            size_t cols = 0;
            for (size_t i = 0; i < span_len && (line_start + (span_start - line_start) + i) < line_end; ++i) {
                char c = file_view[span_start + i];
                cols += (c == '\t') ? 4 : 1;
            }
            caret_len = std::max<size_t>(1, cols);
        }

        std::cout << BOLD_WHITE << m_Lexer.GetPath() << ':' << t->line << ':' << col << ": "
                << RED << "error: " << RESET << msg << '\n';

        std::ostringstream gutter;
        gutter << t->line;
        std::string gutter_str = gutter.str();
        std::cout << gutter_str << " | " << printable_line << '\n';

        std::string underline;
        underline.append(gutter_str.size(), ' ');
        underline.append(" | ");
        underline.append(RED);
        underline.append(caret_pos, ' ');
        underline.append(1, '^');
        underline.append(caret_len - 1, '~');

        std::cout << underline << RESET << '\n';

        std::exit(1);
    }

    template<typename... Args>
    [[noreturn]] void error(std::string_view fmt, Args&&... args) {
        auto args_tuple = std::make_tuple(std::forward<Args>(args)...);

        auto fmt_args = std::apply(
            [](auto&... elems) {
                return std::make_format_args(elems...);
            },
            args_tuple
        );

        std::string msg = std::vformat(fmt, fmt_args);

        error(msg);
    }

    void expect(TokenType type, const std::string& msg) {
        if (!peek().has_value() || peek().value().type != type) {
            error(msg);
        }
        consume();
    }
};
#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <iostream>
#include <optional>
#include <variant>
#include <memory>
#include <functional>
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
};

struct TypeRef {
    struct Primitive { std::string name; };
    struct Named     { std::string name; };
    struct Pointer   { TypeRef *pointee; };
    struct Array     { TypeRef *elem; };

    std::variant<Primitive, Named, Pointer, Array> data;
    bool is_mutable = false;
    bool is_optional = false;
    Symbol *type_symbol = nullptr;

    TypeRef(std::variant<Primitive, Named, Pointer, Array> data, bool mut = false)
        : data(std::move(data)), is_mutable(mut) {}
};

struct Literal {
    enum class Type { INT, FLOAT, STRING, BOOL };
    Type type;
    std::string raw;
    std::variant<int64_t, double, std::string, bool> value;
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
};

struct Param {
    TypeRef *type = nullptr;
    std::string name;
    std::vector<Attribute> attributes;
    Symbol *symbol = nullptr;
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
};

struct Decl {
    std::variant<FnDecl*, VarDecl*, StructDecl*, Include*, Attribute*> data;
    Symbol *symbol = nullptr;
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
};

struct Include {
    std::vector<std::string> path;
    std::string alias;
    Module *resolved = nullptr;
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
};

class Parser {
public:
    Parser(std::vector<Token>& tokens, MemoryArena& arena) 
        : m_Tokens(tokens), m_Arena(arena) {}

    Module *ParseModule();
    Include *ParseInclude();
    Decl *ParseDecl();
    FnDecl *ParseFnDecl(std::vector<Attribute>& out);
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
    std::vector<Token>& m_Tokens;
    MemoryArena& m_Arena;
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
        std::cerr << "Parser Error: " << msg << std::endl;
        std::exit(1);
    }

    void expect(TokenType type, const std::string& msg) {
        if (!peek().has_value() || peek().value().type != type) {
            error(msg);
        }
        consume();
    }
};
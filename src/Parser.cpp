#include "Parser.hpp"

Include *Parser::ParseInclude() {
    auto inc = make<Include>();

    consume(); // Consume the 'include' token

    while (true) {
        auto t = peek();
        if (!t || t->type != TokenType::IDENT) {
            error("Expected include path identifier");
        }
        
        inc->path.push_back(consume().value.value());

        auto next = peek();
        if (next && next->type == TokenType::DOUBLECOLON) {
            consume();
            continue;
        }
        break;
    }

    expect(TokenType::SEMICOLON, "Expected semicolon after include declaration");
    return inc;
}

void Parser::CollectAttributes(std::vector<Attribute>& out) {
    while (auto t = peek()) {
        if (t->type != TokenType::AT) break; 
        
        consume();
        auto name = consume();
        Attribute attr;
        attr.name = name.value.value();

        // handle arguments here
        
        out.push_back(attr);
    }
}

TypeRef *Parser::ParseType() {
    auto first = peek();
    bool is_mut = false;

    if (auto t = peek(); t && peek()->type == TokenType::MUT) {
        consume();
        is_mut = true;
    }
    
    auto t = peek();
    if (!t || t->type != TokenType::IDENT) {
        error("Expected type name");
    }

    std::string type_name = consume().value.value();

    auto base = make<TypeRef>(
        TypeRef::Named{ .name = type_name },
        is_mut
    );

    while (true) {
        if (auto t = peek(); t && t->type != TokenType::MUT && t->type != TokenType::STAR && t->type != TokenType::LBRACK) break;

        bool is_mut = false;
        if (auto t = peek(); t && t->type == TokenType::MUT) {
            consume();
            is_mut = true;
        }

        if (auto t = peek(); t && t->type == TokenType::STAR) {
            Token star_tok = consume();
            
            auto ptr = make<TypeRef>(
                TypeRef::Pointer{ .pointee = base },
                is_mut
            );

            if (auto t = peek(); t && t->type == TokenType::QUESTION) {
                Token q = consume();
                ptr->is_optional = true;
            }

            base = ptr;
            continue;
        }

        if (auto t = peek(); t && t->type == TokenType::LBRACK) {
            Token lb = consume();
            size_t length = 0;
            if (auto t = peek(); t && t->type == TokenType::INT_LIT) {
                consume();
                length = std::strtoul(t->value.value().c_str(), NULL, 10);  // TODO: more bases
            }
            if (auto t = peek(); t && t->type != TokenType::RBRACK) {
                error("Expected ']' for array operator");
            }
            Token rb = consume();

            auto array = make<TypeRef>(
                TypeRef::Array{ .elem = base, .length = length },
                is_mut
            );

            if (auto t = peek(); t && t->type == TokenType::QUESTION) {
                Token q = consume();
                array->is_optional = true;
            }

            base = array;
            continue;
        }

        break;
    }

    return base;
}

static int bp_for_binary(TokenType type, int& right_assoc, BinaryOp& out) {
    right_assoc = 0;

    switch (type) {
        case TokenType::STAR: out = BinaryOp::MUL; return 60;
        case TokenType::SLASH: out = BinaryOp::DIV; return 60;
        case TokenType::PERCENT: out = BinaryOp::MOD; return 60;

        case TokenType::PLUS: out = BinaryOp::ADD; return 50;
        case TokenType::MINUS: out = BinaryOp::SUB; return 50;
        
        case TokenType::SHL: out = BinaryOp::SHL; return 45;
        case TokenType::SHR: out = BinaryOp::SHR; return 45;

        case TokenType::LT: out = BinaryOp::LT; return 40;
        case TokenType::LE: out = BinaryOp::LE; return 40;
        case TokenType::GT: out = BinaryOp::GT; return 40;
        case TokenType::GE: out = BinaryOp::GE; return 40;

        case TokenType::EQEQ: out = BinaryOp::EQ; return 35;
        case TokenType::NEQ: out = BinaryOp::NE; return 35;

        case TokenType::AMP: out = BinaryOp::AND; return 32;
        case TokenType::CARET: out = BinaryOp::XOR; return 28;
        case TokenType::PIPE: out = BinaryOp::OR; return 24;

        case TokenType::AMPAMP: out = BinaryOp::LOG_AND; return 15;
        case TokenType::PIPEPIPE: out = BinaryOp::LOG_OR; return 10;

        case TokenType::EQ: out = BinaryOp::ASSIGN; right_assoc = 1; return 5;

        default: return 0;
    }
}

static int token_to_unary(TokenType type, UnaryOp& out) {
    switch (type) {
        case TokenType::MINUS: out = UnaryOp::NEG; return 1;
        case TokenType::NOT: out = UnaryOp::NOT; return 1;
        case TokenType::STAR: out = UnaryOp::DEREF; return 1;
        case TokenType::AMP: out = UnaryOp::ADDR; return 1;
        default: return 0;
    }
}

Expr *Parser::ExprNewLit(Token& t) {
    auto e = make<Expr>(
        Literal{}
    );

    if (t.type == TokenType::INT_LIT) {
        e->data = Literal{
            .type = Literal::Type::INT,
            .raw = t.value.value(),
            .value = std::strtoll(t.value.value().c_str(), NULL, 0),
        };
    }
    else if (t.type == TokenType::STR_LIT) {
        e->data = Literal{
            .type = Literal::Type::STRING,
            .raw = t.value.value(),
            .value = t.value.value()
        };
    }
    else if (t.type == TokenType::TRUE || t.type == TokenType::FALSE) {
        e->data = Literal{
            .type = Literal::Type::BOOL,
            .raw = t.value.value(),
            .value = (t.type == TokenType::TRUE)
        };
    }
    else {
        error("Unknown expression literal");
    }

    return e;
}

Expr *Parser::ExprNewIdent(Token& t) {
    std::vector<std::string> comps;
    comps.push_back(t.value.value());

    Token last = t;
    while (peek() && peek()->type == TokenType::DOUBLECOLON) {
        consume();
        Token comp = consume();
        if (comp.type != TokenType::IDENT) {
            error("Expected identifier after '::' in path");
        }

        comps.push_back(comp.value.value());
        last = comp;
    }

    Expr *e = nullptr;

    if (comps.size() == 1) {
        e = make<Expr>(
            Expr::Ident{
                .name = comps.at(0),
            }
        );
    } else {
        e = make<Expr>(
            Expr::Path{
                .components = comps
            }
        );
    }

    return e;
}

Expr *Parser::ParseExprPrefix() {
    auto t = peek();

    if (!t) {
        error("Expected a token for expression");
    }

    if (t->type == TokenType::INT_LIT || t->type == TokenType::STR_LIT || t->type == TokenType::CHAR_LIT || t->type == TokenType::TRUE || t->type == TokenType::FALSE) {
        consume();
        return ExprNewLit(t.value());
    }

    if (t->type == TokenType::IDENT) {
        consume();
        return ExprNewIdent(t.value());
    }

    if (t->type == TokenType::LPAREN) {
        consume();

        if (auto t = peek(); t && (t->type == TokenType::IDENT || t->type == TokenType::MUT)) {
            // TODO: type casting
        }
        Expr *inner = ParseExpr();
        expect(TokenType::RPAREN, "Expected matching ')'");
        return inner;
    }

    UnaryOp uop;
    if (token_to_unary(t->type, uop)) {
        consume();
        auto operand = ParseExpr(80);
        auto e = make<Expr>(
            Expr::Unary{.op = uop, .operand = operand}
        );
        return e;
    }

    error("Unexpected token in expression");
}

Expr *Parser::ExprHandlePostfix(Expr *left) {
    while (true) {
        auto next = peek();
        if (next && next->type == TokenType::LPAREN) {
            consume();
            std::vector<Expr*> args;
            if (!peek() || peek()->type != TokenType::RPAREN) {
                while (true) {
                    args.push_back(ParseExpr());

                    if (!peek() || peek()->type != TokenType::COMMA) break;
                    consume();
                }
            }

            consume();
            auto call = make<Expr>(
                Expr::Call{
                    .callee = left,
                    .args = args,
                }
            );
            left = call;
            continue;
        }
        else if (next && next->type == TokenType::LBRACK) {
            Token lb = consume();
            auto idx = ParseExpr();
            Token rb = consume();
            if (rb.type != TokenType::RBRACK) {
                error("Expected matching ']'");
            }

            auto index = make<Expr>(
                Expr::Index{
                    .base = left,
                    .index = idx
                }
            );
            left = index;
            continue;
        }
        else if (next && next->type == TokenType::DOT) {
            consume();
            Token mem = consume();
            if (mem.type != TokenType::IDENT) {
                error("Expected target member after '.'");
            }
            auto m = make<Expr>(
                Expr::Member{
                    .base = left,
                    .member = mem.value.value()
                }
            );
            left = m;
            continue;
        }
        else {
            break;
        }
    }

    return left;
}

Expr *Parser::ParseExpr(int min_bp) {
    Expr *left = ParseExprPrefix();
    left = ExprHandlePostfix(left);

    while (true) {
        auto next = peek();
        if (!next || next->type == TokenType::SEMICOLON || next->type == TokenType::COMMA || next->type == TokenType::RPAREN || next->type == TokenType::RBRACK) {
            break;
        }

        BinaryOp binop;
        int right_assoc = 1;
        int bp = bp_for_binary(next->type, right_assoc, binop);
        if (bp == 0 || bp <= min_bp) break;

        Token op_tok = consume();

        int rbp = right_assoc ? bp - 1 : bp;

        auto right = ParseExpr(rbp);

        auto b = make<Expr>(
            Expr::Binary{.op = binop, .left = left, .right = right}
        );

        left = b;

        left = ExprHandlePostfix(left);
    }

    return left;
}

Stmt *Parser::ParseReturnStmt() {
    consume();
    Expr *value = nullptr;
    if (!peek() || peek()->type != TokenType::SEMICOLON) {
        value = ParseExpr();
    }

    expect(TokenType::SEMICOLON, "Expected semicolon after return");

    auto s = make<Stmt>(
        Stmt::Return{
            .value = value
        }
    );

    return s;
}

Stmt *Parser::ParseForStmt() {
    consume();

    expect(TokenType::LPAREN, "Expected '(' after 'for'");

    Stmt *init = nullptr;
    if (auto t = peek(); !t || t->type != TokenType::SEMICOLON) {
        if (t->type == TokenType::IDENT || t->type == TokenType::MUT) {
            init = ParseVarStmt();
        } else {
            init = ParseExprStmt();
        }
    } else {
        // empty!
    }
    expect(TokenType::SEMICOLON, "Expected ';' after initialiser");

    Expr *cond = nullptr;
    if (auto t = peek(); !t || t->type != TokenType::SEMICOLON) {
        cond = ParseExpr();
    }
    expect(TokenType::SEMICOLON, "Expected ';' after for condition");

    Expr *post = nullptr;
    if (auto t = peek(); !t || t->type != TokenType::RPAREN) {
        post = ParseExpr();
    }
    expect(TokenType::RPAREN, "Expected ')' after for incrementer");

    if (auto t = peek(); !t || t->type != TokenType::LBRACE) {
        error("Expected '{' for for statement");
    }
    Stmt *body = ParseBlockStmt();

    auto s = make<Stmt>(
        Stmt::For{
            .init = init,
            .cond = cond,
            .post = post,
            .body = body
        }
    );

    return s;
}

Stmt *Parser::ParseIfStmt() {
    consume();
    if (auto t = peek(); !t || t->type != TokenType::LPAREN) {
        error("Expected '(' after if");
    }
    auto cond = ParseExpr();

    if (auto t = peek(); !t || t->type != TokenType::LBRACE) {
        error("Expected '{' for if block");
    }
    auto then = ParseBlockStmt();
    Stmt *_else = nullptr;
    if (auto t = peek(); t && t->type == TokenType::ELSE) {
        if (auto t = peek(); !t || t->type != TokenType::LBRACE) {
            error("Expected '{' for else statement");
        }
        _else = ParseBlockStmt();
    }

    auto s = make<Stmt>(
        Stmt::If{
            .cond = cond,
            .then_branch = then,
            .else_branch = _else,
        }
    );

    return s;
}

Stmt *Parser::ParseWhileStmt() {
    if (auto t = peek(); !t || t->type != TokenType::LPAREN) {
        error("Expected '(' after while");
    }
    auto cond = ParseExpr();

    if (auto t = peek(); !t || t->type != TokenType::LBRACE) {
        error("Expected '{' for while statement");
    }
    auto branch = ParseBlockStmt();

    auto s = make<Stmt>(
        Stmt::While{
            .cond = cond,
            .body = branch,
        }
    );

    return s;
}

Stmt *Parser::ParseVarStmt() {
    auto type = ParseType();

    Token name = consume();
    if (name.type != TokenType::IDENT) {
        error("Expected variable name, got '{}'", TokenTypeToString(name.type));
    }

    auto v = make<VarDecl>(
        type,
        name.value.value()
    );

    if (auto t = peek(); t && t->type == TokenType::EQ) {
        consume();
        v->init = ParseExpr();
    }

    auto s = make<Stmt>(
        v
    );

    return s;
}

Stmt *Parser::ParseExprStmt() {
    auto e = ParseExpr();
    expect(TokenType::SEMICOLON, "Expected semicolon after expression");

    auto s = make<Stmt>(e);
    return s;
}

Stmt *Parser::ParseStmt() {
    std::vector<Attribute> attrs;
    CollectAttributes(attrs);

    auto t = peek();

    if (!t) return nullptr; // TODO: this should probably be changed

    switch (t->type) {
        case TokenType::RETURN: return ParseReturnStmt();
        case TokenType::FOR: return ParseForStmt();
        case TokenType::IF: return ParseIfStmt();
        case TokenType::WHILE: return ParseWhileStmt();
        case TokenType::IDENT: {
            if (auto t = peek(1); t && t->type == TokenType::DOUBLECOLON) {
                return ParseExprStmt();
            } 
        }
        case TokenType::MUT: return ParseVarStmt();
        default: break;
    }

    return ParseExprStmt();
}

Stmt *Parser::ParseBlockStmt() {
    consume();

    std::vector<Stmt*> stmts;
    while (peek() && peek()->type != TokenType::RBRACE) {
        stmts.push_back(ParseStmt());
    }

    expect(TokenType::RBRACE, "Expected '}' to close the block");

    auto s = make<Stmt>(
        Stmt::Block{ .stmts = stmts }
    );

    return s;
}

StructDecl *Parser::ParseStructDecl() {
    auto strct = make<StructDecl>();
    CollectAttributes(strct->attributes);
    consume();  // struct

    auto name = consume();
    if (name.type != TokenType::IDENT) {
        error("Expected struct name");
    }
    strct->name = name.value.value();

    expect(TokenType::LBRACE, "Expected '{' for struct");

    while (peek() && peek()->type != TokenType::RBRACE) {
        auto type = ParseType();
        auto name = peek();
        if (!name || name->type != TokenType::IDENT) {
            error("Expected member name");
        }
        consume();
        expect(TokenType::SEMICOLON, "Expected semicolon after member");

        auto decl = make<VarDecl>(type, name.value().value.value());

        strct->members.push_back(decl);
    }

    expect(TokenType::RBRACE, "Expected closing '}' for struct");

    return strct;
}

UnionDecl *Parser::ParseUnionDecl() {
    auto onion = make<UnionDecl>();
    CollectAttributes(onion->attributes);
    consume();  // union

    auto name = consume();
    if (name.type != TokenType::IDENT) {
        error("Expected union name");
    }
    onion->name = name.value.value();

    expect(TokenType::LBRACE, "Expected '{' for union");

    while (peek() && peek()->type != TokenType::RBRACE) {
        auto type = ParseType();
        auto name = peek();
        if (!name || name->type != TokenType::IDENT) {
            error("Expected member name");
        }
        consume();
        expect(TokenType::SEMICOLON, "Expected semicolon after member");

        auto decl = make<VarDecl>(type, name.value().value.value());

        onion->members.push_back(decl);
    }

    expect(TokenType::RBRACE, "Expected closing '}' for union");

    return onion;
}

FnDecl *Parser::ParseFnDecl() {
    auto fn = make<FnDecl>();
    CollectAttributes(fn->attributes);
    consume();  // fn

    TypeRef *ret_type = ParseType();
    Token fn_name = consume();
    if (fn_name.type != TokenType::IDENT) {
        error("Expected function name");
    }
    expect(TokenType::LPAREN, "Missing opening parenthesis for function");

    while (peek() && peek()->type != TokenType::RPAREN) {
        std::vector<Attribute> attrs;
        CollectAttributes(attrs);

        auto param_type = ParseType();
        Token name = consume();
        if (name.type != TokenType::IDENT) {
            error("Expected argument name");
        }

        Param p = (Param){
            .type = param_type,
            .name = name.value.value(),
            .attributes = attrs,
        };

        fn->params.push_back(p);
        if (!peek() || peek()->type != TokenType::COMMA) break;
        consume();
    }
    consume();

    if (peek() && peek()->type == TokenType::LBRACE) {
        fn->body = ParseBlockStmt();
    }

    fn->name = fn_name.value.value();
    fn->ret_type = ret_type;

    return fn;
}

Decl *Parser::ParseDecl() {
    auto d = make<Decl>();
    std::vector<Attribute> attrs;
    CollectAttributes(attrs);

    auto t = peek();
    if (!t) error("Unexpected end of input in declaration");

    if (t->type == TokenType::FN) {
        d->data = ParseFnDecl();
    } else if (t->type == TokenType::STRUCT) {
        d->data = ParseStructDecl();
    } else if (t->type == TokenType::UNION) {
        d->data = ParseUnionDecl();
    } else {
        error("Expected declaration (fn, struct, var)");
    }

    return d;
}

Module *Parser::ParseModule() {
    expect(TokenType::MODULE, "Missing module declaration");
    
    Module *m = make<Module>();

    Token modname = consume();
    if (modname.type != TokenType::IDENT) {
        error("Missing module name");
    }
    m->name = modname.value.value();

    expect(TokenType::SEMICOLON, "Expected semicolon after module declaration");

    while (auto t = peek()) {
        if (t->type != TokenType::INCLUDE) break;
        m->includes.push_back(ParseInclude());
    }

    while (peek().has_value()) {
        m->decls.push_back(ParseDecl());
    }

    return m;
}


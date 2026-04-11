#include "Sema.hpp"

Analyser::Analyser(MemoryArena& arena) : m_Arena(arena) {
    m_GlobalScope = m_Arena.alloc<Scope>();
    m_CurrentScope = m_GlobalScope;

    InjectBuiltinTypes();
}

void Analyser::Analyse(Module* mod) {
    mod->scope = m_GlobalScope;

    RegisterGlobals(mod);
    ResolveTypes(mod);
    CheckBodies(mod);
}

void Analyser::EnterScope(Scope* existing_scope) {
    if (existing_scope) {
        existing_scope->parent = m_CurrentScope;
        m_CurrentScope = existing_scope;
    } else {
        Scope* new_scope = m_Arena.alloc<Scope>();
        new_scope->parent = m_CurrentScope;
        m_CurrentScope = new_scope;
    }
}

void Analyser::LeaveScope() {
    if (m_CurrentScope && m_CurrentScope->parent) {
        m_CurrentScope = m_CurrentScope->parent;
    }
}

Symbol* Analyser::DeclareSymbol(const std::string& name, SymbolFlags flags) {
    for (Symbol* sym : m_CurrentScope->symbols) {
        if (sym->name == name) {
            std::cerr << "Semantic Error: Symbol '" << name << "' is already defined in this scope.\n";
            exit(1);
        }
    }

    Symbol* sym = m_Arena.alloc<Symbol>();
    sym->name = name;
    sym->flags = static_cast<uint32_t>(flags);
    sym->defined_in = m_CurrentScope;
    
    m_CurrentScope->symbols.push_back(sym);
    return sym;
}

Symbol* Analyser::LookupSymbol(const std::string& name) {
    Scope* scope = m_CurrentScope;
    
    while (scope != nullptr) {
        for (Symbol* sym : scope->symbols) {
            if (sym->name == name) {
                return sym;
            }
        }
        scope = scope->parent;
    }
    
    return nullptr;
}

void Analyser::InjectBuiltinTypes() {
    const char* builtins[] = {
        "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64",
        "char", "string",
        "bool",
        "none"
    };

    for (const char* type_name : builtins) {
        Symbol* sym = DeclareSymbol(type_name, SymbolFlags::TYPE);
        
        TypeRef* type_ref = m_Arena.alloc<TypeRef>(
            TypeRef::Named{type_name}, false
        );
        type_ref->type_symbol = sym;
        
        sym->type = type_ref;
    }
}

void Analyser::RegisterGlobals(Module *mod) {
    m_CurrentScope = m_GlobalScope;

    for (auto d : mod->decls) {
        if (auto **fn = std::get_if<FnDecl*>(&d->data)) {
            SymbolFlags flags = SymbolFlags::FN;
            if ((*fn)->is_export) flags = static_cast<SymbolFlags>(static_cast<uint32_t>(flags) | static_cast<uint32_t>(SymbolFlags::EXPORT));

            Symbol *sym = DeclareSymbol((*fn)->name, flags);
            sym->decl = d;
            d->symbol = sym;
            (*fn)->symbol = sym;
        }
        else if (auto **str = std::get_if<StructDecl*>(&d->data)) {
            Symbol *sym = DeclareSymbol((*str)->name, SymbolFlags::TYPE);
            sym->decl = d;
            d->symbol = sym;
            (*str)->symbol = sym;
        }
        else if (auto **unn = std::get_if<UnionDecl*>(&d->data)) {
            Symbol *sym = DeclareSymbol((*unn)->name, SymbolFlags::TYPE);
            sym->decl = d;
            d->symbol = sym;
            (*unn)->symbol = sym;
        }
        else if (auto **var = std::get_if<VarDecl*>(&d->data)) {
            std::cerr << "Semantic error: Global variables are not allowed" << std::endl;
            exit(1);
        }
    }
}

void Analyser::ResolveTypeRef(TypeRef *type) {
    if (!type) return;

    std::visit([&](auto& value) {
        using T = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<T, TypeRef::Named>) {
            Symbol *sym = LookupSymbol(value.name);
            if (!sym || !(sym->flags & (uint32_t)SymbolFlags::TYPE)) {
                std::cout << "Error: Unknown type '" << value.name << "'\n";
                exit(1); 
            }
            type->type_symbol = sym;
        }
        else if constexpr (std::is_same_v<T, TypeRef::Pointer>) {
            ResolveTypeRef(value.pointee);
        }
        else if constexpr (std::is_same_v<T, TypeRef::Array>) {
            ResolveTypeRef(value.elem);
        }
    }, type->data);
}

static size_t GetTypeSize(TypeRef *type) {
    if (!type) return 0;

    return std::visit([&](auto& value) -> size_t {
        using T = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<T, TypeRef::Pointer>) {
            return 8;   // TODO: different pointer sizes on different architectures
        }
        else if constexpr (std::is_same_v<T, TypeRef::Array>) {
            return GetTypeSize(value.elem) * value.length;
        }
        else if constexpr (std::is_same_v<T, TypeRef::Named>) {
            Symbol *sym = type->type_symbol;
            if (!sym) return 0;

            if (sym->name == "int8" || sym->name == "uint8"
             || sym->name == "bool" || sym->name == "char") {
                return 1;
            }
            if (sym->name == "int16" || sym->name == "uint16") {
                return 2;
            }
            if (sym->name == "int32" || sym->name == "uint32") {
                return 4;
            }
            if (sym->name == "int64" || sym->name == "uint64"
             || sym->name == "int"   || sym->name == "uint") {
                return 8;
            }
            if (sym->name == "string") {
                return 16;  // ptr + len
            }

            if (auto **str = std::get_if<StructDecl*>(&sym->decl->data)) {
                return (*str)->size;
            }
            if (auto **unn = std::get_if<UnionDecl*>(&sym->decl->data)) {
                return (*unn)->size;
            }

            return 0;
        }

        return 0;
    }, type->data);
}

static size_t GetTypeAlign(TypeRef *type) {
    if (!type) return 1;

    return std::visit([&](auto& value) -> size_t {
        using T = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<T, TypeRef::Pointer>) {
            return 8;
        }
        else if constexpr (std::is_same_v<T, TypeRef::Array>) {
            return GetTypeAlign(value.elem);
        }
        else if constexpr (std::is_same_v<T, TypeRef::Named>) {
            Symbol *sym = type->type_symbol;
            if (!sym) return 1;

            if (sym->name == "int8" || sym->name == "uint8"
             || sym->name == "bool" || sym->name == "char") {
                return 1;
            }
            if (sym->name == "int16" || sym->name == "uint16") {
                return 2;
            }
            if (sym->name == "int32" || sym->name == "uint32") {
                return 4;
            }
            if (sym->name == "int64" || sym->name == "uint64"
             || sym->name == "int"   || sym->name == "uint") {
                return 8;
            }
            if (sym->name == "string") {
                return 8;  // ptr + len
            }
            if (auto **str = std::get_if<StructDecl*>(&sym->decl->data)) {
                return (*str)->align;
            }
            if (auto **unn = std::get_if<UnionDecl*>(&sym->decl->data)) {
                return (*unn)->align;
            }
            return 1;
        }
        return 1;
    }, type->data);
}

static void CalculateStructLayout(StructDecl *str) {
    size_t cur_offset = 0;
    size_t max_align = 1;

    for (auto m : str->members) {
        size_t size = GetTypeSize(m->type);
        size_t align = GetTypeAlign(m->type);

        if (align > max_align) max_align = align;

        if (cur_offset % align != 0) {
            cur_offset += (align - (cur_offset % align));
        }

        str->field_offsets.push_back(cur_offset);

        cur_offset += size;
    }

    if (cur_offset % max_align != 0) {
        cur_offset += (max_align - (cur_offset % max_align));
    }

    str->size = cur_offset;
    str->align = max_align;
}

static void CalculateUnionLayout(UnionDecl *unn) {
    size_t max_offset = 0;
    size_t max_align = 1;

    for (auto m : unn->members) {
        size_t size = GetTypeSize(m->type);
        size_t align = GetTypeAlign(m->type);

        if (align > max_align) max_align = align;
        if (size > max_offset) max_offset = size;
    }

    unn->size = max_offset;
    unn->align = max_align;
}

void Analyser::ResolveTypes(Module *mod) {
    m_CurrentScope = m_GlobalScope;

    for (auto d : mod->decls) {
        if (auto **fn = std::get_if<FnDecl*>(&d->data)) {
            ResolveTypeRef((*fn)->ret_type);

            for (auto& p : (*fn)->params) {
                ResolveTypeRef(p.type);
            }
        }
        else if (auto **str = std::get_if<StructDecl*>(&d->data)) {
            for (auto m : (*str)->members) {
                ResolveTypeRef(m->type);
            }

            CalculateStructLayout(*str);
        }
        else if (auto **unn = std::get_if<UnionDecl*>(&d->data)) {
            for (auto m : (*unn)->members) {
                ResolveTypeRef(m->type);
            }

            CalculateUnionLayout(*unn);
        }
        else if (auto **var = std::get_if<VarDecl*>(&d->data)) {
            std::cerr << "Semantic error: Global variables are not allowed" << std::endl;
            exit(1);
        }
    }
}

void Analyser::CheckStmt(Stmt *stmt) {
    if (!stmt) return;

    stmt->scope = m_CurrentScope;

    std::visit([&](auto& value) {
        using T = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<T, Stmt::Block>) {
            EnterScope();
            for (auto s : value.stmts) {
                CheckStmt(s);
            }
            LeaveScope();
        }
        else if constexpr (std::is_same_v<T, VarDecl*>) {
            VarDecl *var = value;
            ResolveTypeRef(var->type);

            if (var->init) {
                TypeRef *init_type = CheckExpr(var->init);

                if (!TypesEqual(var->type, init_type)) {
                    std::cerr << "Type Error: Cannot assign variable '" << var->name << "' of type '" << var->type->type_symbol->name << "' to a different type '" << init_type->type_symbol->name << "'" << std::endl;
                    exit(1);
                }
            }

            SymbolFlags flags = SymbolFlags::VAR;
            if (var->is_mutable) flags = static_cast<SymbolFlags>(static_cast<uint32_t>(flags) | static_cast<uint32_t>(SymbolFlags::MUT));

            Symbol *sym = DeclareSymbol(var->name, flags);
            sym->type = var->type;
            var->symbol = sym;
        }
        else if constexpr (std::is_same_v<T, Stmt::Return>) {
            if (value.value) {
                TypeRef *ret_type = CheckExpr(value.value);
                if (!TypesEqual(ret_type, m_CurrentFunction->ret_type)) {
                    std::cerr << "Type Error: Returned value does not match function '" << m_CurrentFunction->name << "' return type" << std::endl;
                    exit(1);
                }
            } else {
                if (m_CurrentFunction->ret_type && m_CurrentFunction->ret_type->type_symbol->name != "none") {
                    std::cerr << "Type Error: Function '" << m_CurrentFunction->name << "' expects a return value" << std::endl;
                    exit(1);
                }
            }
        }
        else if constexpr (std::is_same_v<T, Expr*>) {
            CheckExpr(value);
        }
        else if constexpr (std::is_same_v<T, Stmt::If>) {
            if (value.cond) {
                TypeRef *cond_type = CheckExpr(value.cond);
                if (!cond_type || cond_type->type_symbol->name != "bool") {
                    std::cerr << "Type Error: 'if' condition must evaluate to a boolean" << std::endl;
                    exit(1);
                }
            }

            CheckStmt(value.then_branch);
            if (value.else_branch) {
                CheckStmt(value.else_branch);
            }
        }
        else if constexpr (std::is_same_v<T, Stmt::While>) {
            if (value.cond) {
                TypeRef *cond_type = CheckExpr(value.cond);
                if (!cond_type || cond_type->type_symbol->name != "bool") {
                    std::cerr << "Type Error: 'while' condition must evaluate to a boolean" << std::endl;
                    exit(1);
                }
            }

            CheckStmt(value.body);
        }
        else if constexpr (std::is_same_v<T, Stmt::For>) {
            EnterScope();   // traps the initialiser for the entirety of the loops

            if (value.init) {
                CheckStmt(value.init);
            }

            if (value.cond) {
                TypeRef *cond_type = CheckExpr(value.cond);
                if (!cond_type || cond_type->type_symbol->name != "bool") {
                    std::cerr << "Type Error: 'for' condition must evaluate to a boolean" << std::endl;
                    exit(1);
                }
            }

            if (value.post) {
                CheckExpr(value.post);
            }

            if (value.body) {
                CheckStmt(value.body);
            }

            LeaveScope();
        }
    }, stmt->data);
}

void Analyser::CheckFunctionBody(FnDecl *fn) {
    if (!fn->body) return;

    m_CurrentFunction = fn;

    EnterScope();
    fn->local_scope = m_CurrentScope;

    for (auto& p : fn->params) {
        Symbol *sym = DeclareSymbol(p.name, SymbolFlags::VAR);
        sym->type = p.type;
        p.symbol = sym;
    }
    
    CheckStmt(fn->body);

    LeaveScope();
    m_CurrentFunction = nullptr;
}

void Analyser::CheckBodies(Module *mod) {
    m_CurrentScope = m_GlobalScope;

    for (auto d : mod->decls) {
        if (auto **fn = std::get_if<FnDecl*>(&d->data)) {
            CheckFunctionBody(*fn);
        }
    }
}

TypeRef *Analyser::CheckExpr(Expr *expr) {
    if (!expr) return nullptr;

    TypeRef *result_type = std::visit([&](auto& value) -> TypeRef* {
        using T = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<T, Literal>) {
            std::string type_name;
            switch (value.type) {
                case Literal::Type::INT: type_name = "int"; break;
                case Literal::Type::BOOL: type_name = "bool"; break;
                case Literal::Type::STRING: type_name = "string"; break;
                case Literal::Type::CHAR: type_name = "char"; break;
            }
            Symbol *type_sym = LookupSymbol(type_name);
            return type_sym ? type_sym->type : nullptr;
        }
        else if constexpr (std::is_same_v<T, Expr::Ident>) {
            Symbol *sym = LookupSymbol(value.name);
            if (!sym) {
                std::cerr << "Error: Undefined variable '" << value.name << "'\n";
                exit(1);
            }
            expr->symbol = sym;
            return sym->type;
        }
        else if constexpr (std::is_same_v<T, Expr::Binary>) {
            TypeRef *left_t = CheckExpr(value.left);
            TypeRef *right_t = CheckExpr(value.right);

            // TODO: allow implicit widening, but not shortening
            // for now just be strict
            if (left_t && right_t && left_t->type_symbol != right_t->type_symbol) {
                std::cerr << "Type Error: Cannot apply operator to different types\n";
                exit(1);
            }

            if (value.op == BinaryOp::EQ || value.op == BinaryOp::LT || value.op == BinaryOp::LE
             || value.op == BinaryOp::GT || value.op == BinaryOp::GE) {
                return LookupSymbol("bool")->type;
            }

            if (value.op == BinaryOp::ASSIGN) { //TODO: ADD_ASSIGN and others
                if (!TypesEqual(left_t, right_t)) {
                    std::cerr << "Type Error: Cannot assign value of different type" << std::endl;
                    exit(1);
                }

                if (!left_t->is_mutable) {
                    std::cerr << "Type Error: Cannot assign to immutable variable" << std::endl;
                    exit(1);
                }

                return left_t;
            }

            return left_t;
        }
        else if constexpr (std::is_same_v<T, Expr::Call>) {
            CheckExpr(value.callee);

            Symbol *callee_sym = value.callee->symbol;
            if (!callee_sym) {
                std::cerr << "Type Error: unresolved function call '" << callee_sym->name << "'" << std::endl;
                exit(1);
            }

            if (!(callee_sym->flags & static_cast<uint32_t>(SymbolFlags::FN))) {
                std::cerr << "Type Error: Attempted to call non-function '" << callee_sym->name << "'" << std::endl;
                exit(1);
            }

            auto **fn_ptr = std::get_if<FnDecl*>(&callee_sym->decl->data);
            if (!fn_ptr) {
                std::cerr << "Internal Error: Function symbol missing declaration" << std::endl;
                exit(1);
            }
            FnDecl *fn = *fn_ptr;

            if (value.args.size() != fn->params.size()) {
                std::cerr << "Type Error: Function '" << fn->name 
                          << "' expects " << fn->params.size() << " arguments, but got " << value.args.size() << std::endl;
                exit(1);
            }

            for (size_t i = 0; i < value.args.size(); i++) {
                TypeRef *arg_type = CheckExpr(value.args[i]);
                TypeRef *param_type = fn->params[i].type;

                if (!TypesEqual(arg_type, param_type)) {
                    std::cerr << "Type Error: Argument " << (i + 1) << " for function '"
                              << fn->name << "' has incorrect type" << std::endl;
                    exit(1);
                }
            }

            return fn->ret_type;
        }
        else if constexpr (std::is_same_v<T, Expr::Member>) {
            TypeRef *base_type = CheckExpr(value.base);
            if (!base_type) return nullptr;

            // TODO: decide if we allow the '.' operator on pointers
            // if (std::holds_alternative<TypeRef::Pointer>(base_type->data)) {
            //     base_type = std::get<TypeRef::Pointer>(base_type->data).pointee;
            // }

            Symbol *type_sym = base_type->type_symbol;
            if (!type_sym || !(type_sym->flags & static_cast<uint32_t>(SymbolFlags::TYPE))) {
                std::cerr << "Type Error: Base of member access is not a valid type" << std::endl;
                exit(1);
            }

            StructDecl *str = nullptr;
            UnionDecl *unn = nullptr;

            if (auto **s = std::get_if<StructDecl*>(&type_sym->decl->data)) str = *s;
            else if (auto **u = std::get_if<UnionDecl*>(&type_sym->decl->data)) unn = *u;

            if (!str && !unn) {
                std::cerr << "Type Error: Type '" << type_sym->name << "' does not have members" << std::endl;
                exit(1);
            }

            std::vector<VarDecl*>& members = str ? str->members : unn->members;
            for (size_t i = 0; i < members.size(); i++) {
                if (members[i]->name == value.member) {
                    return members[i]->type;
                }
            }

            std::cerr << "Type Error: Member '" << value.member 
                      << "' not found in type '" << type_sym->name << std::endl;
            exit(1);
        }
        else if constexpr (std::is_same_v<T, Expr::Unary>) {
            TypeRef *operand_type = CheckExpr(value.operand);
            if (!operand_type) return nullptr;

            switch (value.op) {
                case UnaryOp::NEG: {
                    std::string name = operand_type->type_symbol->name;
                    if (name.find("int") == std::string::npos) {
                        std::cerr << "Type Error: Cannot negate non-numeric type '" << name << "'" << std::endl;
                        exit(1);
                    }
                    return operand_type;
                }
                case UnaryOp::NOT: {
                    if (operand_type->type_symbol->name != "bool") {
                        std::cerr << "Type Error: Logical NOT requires a boolean" << std::endl;
                        exit(1);
                    }
                    return operand_type;
                }
                case UnaryOp::ADDR: {
                    return m_Arena.alloc<TypeRef>(TypeRef::Pointer{operand_type}, false);
                }
                case UnaryOp::DEREF: {
                    if (auto *ptr_data = std::get_if<TypeRef::Pointer>(&operand_type->data)) {
                        return ptr_data->pointee;
                    }
                    std::cerr << "Type Error: Cannot dereference non-pointer type" << std::endl;
                    exit(1);
                }
            }
        }
        else if constexpr (std::is_same_v<T, Expr::Index>) {
            TypeRef *base_type = CheckExpr(value.base);
            TypeRef *index_type = CheckExpr(value.index);

            if (!index_type || (index_type->type_symbol->name.find("int") == std::string::npos)) {
                std::cerr << "Type Error: Array index must be an integer" << std::endl;
                exit(1);
            }

            return std::visit([&](auto& value) -> TypeRef* {
                using T = std::decay_t<decltype(value)>;

                // TODO: you may only index a pointer if in an `unsafe` clause
                if constexpr (std::is_same_v<T, TypeRef::Pointer>) {
                    return value.pointee;
                }
                else if constexpr (std::is_same_v<T, TypeRef::Array>) {
                    return value.elem;
                }
                else {
                    std::cerr << "Type Error: Cannot index into non-array/non-pointer type" << std::endl;
                    exit(1);
                }
            }, base_type->data);
        }
        else if constexpr (std::is_same_v<T, Expr::Cast>) {
            CheckExpr(value.expr);

            ResolveTypeRef(value.to);

            // TODO: currently allows any cast, should enforce rules
            return value.to;
        }

        return nullptr;
    }, expr->data);

    expr->resolved_type = result_type;
    return result_type;
}

bool Analyser::TypesEqual(TypeRef *a, TypeRef *b) {
    if (a == b) return true;

    if (!a || !b) return false;

    if (a->is_optional != b->is_optional) return false;

    if (a->data.index() != b->data.index()) return false;

    return std::visit([&](auto& a_val) -> bool {
        using T = std::decay_t<decltype(a_val)>;

        if constexpr (std::is_same_v<T, TypeRef::Named>) {
            auto& b_val = std::get<TypeRef::Named>(b->data);
            return a->type_symbol == b->type_symbol;
        }
        else if constexpr (std::is_same_v<T, TypeRef::Pointer>) {
            auto& b_val = std::get<TypeRef::Pointer>(b->data);
            return TypesEqual(a_val.pointee, b_val.pointee);
        }
        else if constexpr (std::is_same_v<T, TypeRef::Array>) {
            auto& b_val = std::get<TypeRef::Array>(b->data);
            return (a_val.length == b_val.length) && TypesEqual(a_val.elem, b_val.elem);
        }
        return false;
    }, a->data);
}
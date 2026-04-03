#include "Sema.hpp"

Scope *Analyser::NewScope(Scope *parent) {
    Scope *s = m_Module->arena.alloc<Scope>();
    s->parent = parent;
    return s;
}

void Analyser::CreateSymbolTable() {
    m_Module->scope = NewScope(nullptr);
    for (auto& d : m_Module->decls) {
        auto s = m_Module->arena.alloc<Symbol>();
        s->defined_in = m_Module->scope;
        s->decl = d;
        
        std::visit([s](auto const& value) {
            s->name = value->name;
        }, d->data);

        m_Module->scope->symbols.push_back(s);

        if (std::is_same_v<std::decay_t<decltype(d)>, FnDecl*>) {
            FnDecl *fn = std::get<FnDecl*>(d->data);
            fn->local_scope = NewScope(m_Module->scope);
            for (auto& p : fn->params) {
                auto s = m_Module->arena.alloc<Symbol>();
                s->name = p.name;
                s->type = p.type;
                fn->local_scope->symbols.push_back(s);
            }
        }
    }
}
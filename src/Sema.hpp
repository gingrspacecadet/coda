#pragma once

#include "Parser.hpp"
#include "pch.hpp"

class Analyser {
public:
    explicit Analyser(MemoryArena& arena);

    void Analyse(Module* mod);

private:
    MemoryArena& m_Arena;
    
    Scope* m_GlobalScope = nullptr;
    Scope* m_CurrentScope = nullptr;
    FnDecl* m_CurrentFunction = nullptr;
    
    void EnterScope(Scope* existing_scope = nullptr);
    void LeaveScope();
    
    Symbol* DeclareSymbol(const std::string& name, SymbolFlags flags);
    Symbol* LookupSymbol(const std::string& name);

    void InjectBuiltinTypes();

    void RegisterGlobals(Module* mod);
    void ResolveTypes(Module* mod);
    void ResolveTypeRef(TypeRef *type);
    void CheckBodies(Module* mod);
};
#pragma once

#include "pch.hpp"
#include "Parser.hpp"

class Analyser {
public:
    Analyser(Module *module)
        : m_Module(module) {}

    Scope *NewScope(Scope *parent);
    void CreateSymbolTable();
private:
    Module *m_Module = nullptr;

    template<typename T, typename... Args>
    T *make(Args&&... args) {
        return m_Module->arena.alloc<T>(std::forward<Args>(args)...);
    }
};
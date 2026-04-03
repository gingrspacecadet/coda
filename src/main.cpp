#include "pch.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"

extern void Emit(Module *module);

int main(void) {
    Parser parser("test/main.coda");
    Module *module = parser.ParseModule();

    // module->Print();

    Emit(module);
}
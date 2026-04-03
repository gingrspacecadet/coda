#include "Lexer.hpp"
#include "Parser.hpp"

int main(void) {
    Parser parser("test/idea.coda");
    Module *module = parser.ParseModule();

    module->Print();
}
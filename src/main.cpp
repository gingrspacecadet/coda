#include <iostream>
#include "Lexer.hpp"
#include "Parser.hpp"

int main(void) {
    Lexer lexer("test/idea.coda");

    std::vector<Token> tokens = lexer.Lex();
    
    MemoryArena arena;
    Parser parser(tokens, arena);
    Module *module = parser.ParseModule();
}
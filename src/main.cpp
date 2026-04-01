#include <iostream>
#include "Lexer.hpp"

int main(void) {
    Coda::Lexer lexer("test/idea.coda");

    std::vector<Coda::Token> tokens = lexer.Lex();
    for (const auto& t : tokens) {
        std::cout << t << ' ';
    }
    std::cout << std::endl;
}
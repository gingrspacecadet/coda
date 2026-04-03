#include "pch.hpp"
#include "Parser.hpp"

void Emit(Module *module) {
    std::fstream out("out.S", std::ios::out);

    out << "global _start\n"
        << "extern main\n"
        << "section .text\n"
        << "_start:\n"
        << "    call main\n"    // TODO: string[] args support
        << "    mov rdi, rax\n"
        << "    mov rax, 60\n"
        << "    syscall\n";

    for (auto& d : module->decls) {
        std::visit([&out](auto& value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, FnDecl*>) {
                out << value->name << ":\n";
                std::visit([&out](auto& value) {
                    using T = std::decay_t<decltype(value)>;

                    if constexpr (std::is_same_v<T, Stmt::Block>) {
                        for (auto& s : value.stmts) {
                            std::visit([&out](auto& value) {
                                using T = std::decay_t<decltype(value)>;

                                if constexpr (std::is_same_v<T, Stmt::Return>) {
                                    std::visit([&out](auto& value) {
                                        using T = std::decay_t<decltype(value)>;

                                        if constexpr (std::is_same_v<T, Literal>) {
                                            out << "    mov rax, " << std::get<int64_t>(value.value) << "\n"
                                                << "    ret\n";
                                        } 
                                        else if constexpr (std::is_same_v<T, Expr::Call>) {
                                            
                                        } else {

                                        }
                                    }, value.value->data); 
                                } else {

                                }
                            }, s->data);
                        }
                    } else { 
                        std::cout << "unknown stmt" << std::endl;
                    }
                }, value->body->data);
            } else {
                // TODO: more decls
                std::cout << "unknown decl" << std::endl;
            }
        }, d->data);
    }
}
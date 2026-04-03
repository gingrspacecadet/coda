#include "Parser.hpp"

static void PrintIndent(int indent) {
    for (int i = 0; i < indent; i++) std::cout.put(' ');
}

void Attribute::Print(int indent) {
    PrintIndent(indent + 2);
    std::cout << '@' << name;
    if (args.size() > 0) {
        std::cout << '(';
        for (size_t i = 0; i < args.size(); i++) {
            if (i) std::cout << ", ";
            std::cout << args.at(i);
        }
    }
}

void TypeRef::Print(int indent) {
    bool pointer = false;
    bool array = false;
    std::cout << ": type ";
    std::visit([&pointer, &array](auto& value) {
        using T = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<T, Named>) {
            std::cout << value.name;
        }
        else if constexpr (std::is_same_v<T, Pointer>) {
            std::cout << '*';
            pointer = true;
        }
        else if constexpr (std::is_same_v<T, Array>) {
            std::cout << "array";
            std::cout << " (len " << value.length << ")";
            array = true;
        } else {
            std::cout << "<unknown>";
        }
    }, data);

    if (is_mutable) std::cout << " mut";
    if (is_optional) std::cout << " opt";
    if (pointer) std::get<Pointer>(data).pointee->Print(0);
    if (array) std::get<Array>(data).elem->Print(0);
}

void VarDecl::Print(int indent) {
    PrintIndent(indent + 2);
    std::cout << "Var " << name;
    if (type) {
        type->Print(0);
    }

    std::cout << '\n';
}

void StructDecl::Print(int indent) {
    PrintIndent(indent + 2);
    std::cout << "Struct: " << name << '\n';
    for (auto& m : members) {
        m->Print(indent + 2);
    }
}

void UnionDecl::Print(int indent) {
    PrintIndent(indent + 2);
    std::cout << "Union: " << name << '\n';
    for (auto& m : members) {
        m->Print(indent + 2);
    }
}

void Param::Print(int indent) {
    PrintIndent(indent + 2);
    std::cout << "- Param '" << name << '\'';
    if (type) type->Print(0);
    std::cout << '\n';
    for (auto& a : attributes) {
        a.Print(indent + 2);
    }
}

static std::string UOpToStr(UnaryOp op) {
    switch (op) {
        case UnaryOp::NEG: return "-";
        case UnaryOp::NOT: return "!";
        case UnaryOp::DEREF: return "*";
        case UnaryOp::ADDR: return "&";
        default: return "<unknown>";
    }
}

static std::string BinOpToStr(BinaryOp op) {
    switch (op) {
        case BinaryOp::MUL: return "*";
        case BinaryOp::DIV: return "/";
        case BinaryOp::MOD: return "%";
        case BinaryOp::ADD: return "+";
        case BinaryOp::SUB: return "-";
        case BinaryOp::SHL: return "<<";
        case BinaryOp::SHR: return ">>";
        case BinaryOp::LT: return "<";
        case BinaryOp::LE: return "<=";
        case BinaryOp::GT: return ">";
        case BinaryOp::GE: return ">=";
        case BinaryOp::EQ: return "==";
        case BinaryOp::NE: return "!=";
        case BinaryOp::AND: return "&";
        case BinaryOp::XOR: return "^";
        case BinaryOp::OR: return "|";
        case BinaryOp::LOG_AND: return "&&";
        case BinaryOp::LOG_OR: return "||";
        case BinaryOp::ASSIGN: return "=";
        case BinaryOp::ADD_ASSIGN: return "+=";
        default: return "<unknown>";
    }
}

static void PrintLit(const Literal& lit, int indent) {
    PrintIndent(indent + 2);
    switch (lit.type) {
        case Literal::Type::INT: {
            std::cout << "int: " << std::get<int64_t>(lit.value);
            break;
        }
        case Literal::Type::FLOAT: {
            std::cout << "float: " << std::get<double>(lit.value);
            break;
        }
        case Literal::Type::BOOL: {
            std::cout << "bool: " << std::get<bool>(lit.value);
            break;
        }
        case Literal::Type::STRING: {
            std::cout << "string: " << std::get<std::string>(lit.value);
            break;
        }
        case Literal::Type::CHAR: {
            std::cout << "char: " << std::get<char>(lit.value);
            break;
        }
    }

    std::cout << '\n';
}

void Expr::Print(int indent) {
    PrintIndent(indent + 2);

    std::visit([indent](auto& value) {
        using T = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<T, Literal>) {
            std::cout << "Literal: ";
            PrintLit(value, -2);
        }
        else if constexpr (std::is_same_v<T, Ident>) {
            std::cout << "Ident: " << value.name << '\n';
        }
        else if constexpr (std::is_same_v<T, Path>) {
            std::cout << "Path: ";
            for (size_t i = 0; i < value.components.size(); i++) {
                if (i) std::cout << "::";
                std::cout << value.components.at(i);
            }
            std::cout << '\n';
        }
        else if constexpr (std::is_same_v<T, Unary>) {
            std::cout << "Unary op " << UOpToStr(value.op) << '\n';
            value.operand->Print(indent + 2);
        }
        else if constexpr (std::is_same_v<T, Binary>) {
            std::cout << "Binary op " << BinOpToStr(value.op) << '\n';
            value.left->Print(indent + 2);
            value.left->Print(indent + 2);
        }
        else if constexpr (std::is_same_v<T, Call>) {
            std::cout << "Call:\n";
            value.callee->Print(indent + 2);
            PrintIndent(indent + 4);
            std::cout << "Args:\n";
            for (auto& a : value.args) {
                a->Print(indent + 4);
            }
        }
        else {
            std::cout << "<unknown>\n";
        }
    }, data);
}

void Stmt::Print(int indent) {
    PrintIndent(indent + 2);
    std::visit([indent](auto& value) {
        using T = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<T, Block>) {
            std::cout << "Block:\n";
            for (auto& s : value.stmts) {
                s->Print(indent + 2);
            }
        }
        else if constexpr (std::is_same_v<T, Return>) {
            std::cout << "Return:\n";
            value.value->Print(indent + 2);
        }
        else if constexpr (std::is_same_v<T, If>) {
            std::cout << "If\n";
            if (value.cond) value.cond->Print(indent + 2);
            if (value.then_branch) value.then_branch->Print(indent + 2);
            if (value.else_branch) {
                PrintIndent(indent + 2);
                std::cout << "Else:";
                value.else_branch->Print(indent + 2);
            }
        }
        else if constexpr (std::is_same_v<T, For>) {
            std::cout << "For:\n";
            if (value.init) value.init->Print(indent + 2);
            if (value.cond) value.cond->Print(indent + 2);
            if (value.post) value.post->Print(indent + 2);
            value.body->Print(indent + 2);
        }
        else if constexpr (std::is_same_v<T, While>) {
            std::cout << "While:\n";
            value.cond->Print(indent + 2);
            value.body->Print(indent + 2);
        }
        else if constexpr (std::is_same_v<T, Unsafe>) {
            std::cout << "Unsafe:\n";
            for (auto& s : value.stmts) {
                s->Print(indent + 2);
            }
        }
        else if constexpr (std::is_same_v<T, VarDecl*>) {
            std::cout << "VarDecl:\n";
            value->Print(indent + 2);
        }
        else if constexpr (std::is_same_v<T, Expr*>) {
            std::cout << "Expr:\n";
            value->Print(indent + 2);
        }
        else {
            std::cout << "Unknown\n";
        }
    }, data);
}

void FnDecl::Print(int indent) {
    PrintIndent(indent + 2);
    std::cout << "Function: " << name << '\n';
    for (auto& a : attributes) {
        a.Print(indent + 2);
    }

    PrintIndent(indent + 2);
    std::cout << "Return type";
    ret_type->Print(0);
    std::cout << '\n';

    PrintIndent(indent + 2);
    std::cout << "Parameters:" << '\n';
    for (auto& p : params) {
        p.Print(indent + 2);
    }

    PrintIndent(indent + 2);
    std::cout << "Body:\n";
    if (body) {
        body->Print(indent + 2);
    } else {
        PrintIndent(indent + 2);
        std::cout << "<no body>";
    }
}

void Decl::Print(int indent) {
    PrintIndent(indent + 2);
    std::cout << "Decl: type = " << data.index() << '\n';
    std::visit([indent](auto& value) {
        value->Print(indent + 2);
    }, data);
}

void Include::Print(int indent) {
    PrintIndent(indent);
    std::cout << "Include:";
    if (alias) std::cout << " (alias " << alias.value() << ")";
    std::cout << '\n';

    PrintIndent(indent + 2);
    std::cout << "Path: ";
    if (path.size() == 0) {
        std::cout << "<empty>";
    } else {
        for (size_t i = 0; i < path.size(); i++) {
            if (i) std::cout << "::";
            std::cout << path.at(i);
        }
    }
    std::cout << '\n';
}

void Module::Print(int indent) {
    std::cout << "=== Module ===\n";
    std::cout << "Name: " << name << '\n';
    std::cout << "Includes: (total " << includes.size() << "):\n";
    for (const auto& i : includes) {
        i->Print(indent + 2);
    }
    for (const auto& d : decls) {
        d->Print(indent);
    }
    std::cout << "=== End ===\n";
}
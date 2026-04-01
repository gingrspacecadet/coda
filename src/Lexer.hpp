#include <string>
#include <optional>
#include <vector>

namespace Coda {
    enum class TokenType {
        MODULE,
        IDENT,
        SEMI,
        NUMBER,
    };

    struct Token {
        TokenType type;
        std::optional<std::string> value {};
    };

    class Lexer {
    public:
        Lexer(const std::string& path);

        std::vector<Token> Lex();

    private:
        const std::string& m_Path; 
        std::string m_FileContents;
        size_t m_Index;

        [[nodiscard]] std::optional<char> peek(const size_t offset = 0) const {
            if (m_Index + offset >= m_FileContents.length()) {
                return {};
            }
            return m_FileContents.at(m_Index + offset);
        }

        char consume() {
            return m_FileContents.at(m_Index++);
        }
    };
}
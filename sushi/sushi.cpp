﻿#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace sushi {
    enum Token {
        EOFToken = -1,

        // commands
        DefToken = -2,
        ExternToken = -3,

        // primary
        IdentifierToken = -4,
        NumberToken = -5
    };

    static std::string IdentifierStr; // filled in if tok_identifier
    static double NumVal;             // filled in if tok_number

    /// gettok - Return the next token from standard input.
    static int gettok() {
        static int LastChar = ' ';

        // Skip any whitespace.
        while (isspace(LastChar))
            LastChar = getchar();

        if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
            IdentifierStr = LastChar;
            while (isalnum((LastChar = getchar())))
                IdentifierStr += LastChar;

            if (IdentifierStr == "def")
                return DefToken;
            if (IdentifierStr == "extern")
                return ExternToken;
            return IdentifierToken;
        }

        if (isdigit(LastChar) || LastChar == '.') { // Number: [0-9.]+
            std::string NumStr;
            do {
                NumStr += LastChar;
                LastChar = getchar();
            } while (isdigit(LastChar) || LastChar == '.');

            NumVal = strtod(NumStr.c_str(), nullptr);
            return NumberToken;
        }

        if (LastChar == '#') {
            // Comment until end of line.
            do
                LastChar = getchar();
            while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

            if (LastChar != EOF)
                return gettok();
        }

        if (LastChar == EOF)
            return EOFToken;

        int ThisChar = LastChar;
        LastChar = getchar();
        return ThisChar;
    }

    /// AST

    namespace {

        class ExprAST {
          public:
            virtual ~ExprAST() = default;
        };

        class NumberExprAST : public ExprAST {
            double Val;

          public:
            NumberExprAST(double Val) : Val(Val) {}
        };

        class VariableExprAST : public ExprAST {
            std::string Name;

          public:
            VariableExprAST(const std::string &Name) : Name(Name) {}
        };

        class BinaryExprAST : public ExprAST {
            char Op;
            std::unique_ptr<ExprAST> LHS, RHS;

          public:
            BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                          std::unique_ptr<ExprAST> RHS)
                : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
        };

        class CallExprAST : public ExprAST {
            std::string Callee;
            std::vector<std::unique_ptr<ExprAST>> Args;

          public:
            CallExprAST(const std::string &Callee,
                        std::vector<std::unique_ptr<ExprAST>> Args)
                : Callee(Callee), Args(std::move(Args)) {}
        };

        class PrototypeAST {
            std::string Name;
            std::vector<std::string> Args;

          public:
            PrototypeAST(const std::string &Name, std::vector<std::string> Args)
                : Name(Name), Args(std::move(Args)) {}

            const std::string &getName() const { return Name; }
        };

        class FunctionAST {
            std::unique_ptr<PrototypeAST> Proto;
            std::unique_ptr<ExprAST> Body;

          public:
            FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                        std::unique_ptr<ExprAST> Body)
                : Proto(std::move(Proto)), Body(std::move(Body)) {}
        };

    } // namespace

    static int CurTok;
    static int getNextToken() { return CurTok = gettok(); }

    static std::map<char, int> BinopPrecedence;
    static int GetTokPrecedence() {
        if (!isascii(CurTok))
            return -1;

        int TokPrec = BinopPrecedence[CurTok];
        if (TokPrec <= 0)
            return -1;
        return TokPrec;
    }

    std::unique_ptr<ExprAST> LogError(const char *Str) {
        fprintf(stderr, "Error: %s\n", Str);
        return nullptr;
    }
    std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
        LogError(Str);
        return nullptr;
    }

    class Parser {
      public:
        /// numberexpr ::= number
        static std::unique_ptr<ExprAST> ParseNumberExpr() {
            auto Result = std::make_unique<NumberExprAST>(NumVal);
            getNextToken();
            return std::move(Result);
        }

        /// parenexpr ::= '(' expression ')'
        static std::unique_ptr<ExprAST> ParseParenExpr() {
            getNextToken(); // eat (.
            auto V = ParseExpression();
            if (!V)
                return nullptr;

            if (CurTok != ')')
                return LogError("expected ')'");
            getNextToken(); // eat ).
            return V;
        }

        /// identifierexpr
        ///   ::= identifier
        ///   ::= identifier '(' expression* ')'
        static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
            std::string IdName = IdentifierStr;

            getNextToken();

            if (CurTok != '(')
                return std::make_unique<VariableExprAST>(IdName);

            // Its a call
            getNextToken();
            std::vector<std::unique_ptr<ExprAST>> Args;
            if (CurTok != ')') {
                while (true) {
                    if (auto Arg = ParseExpression())
                        Args.push_back(std::move(Arg));
                    else
                        return nullptr;

                    if (CurTok == ')')
                        break;

                    if (CurTok != ',')
                        return LogError("Expected ')' or ',' in argument list");
                    getNextToken();
                }
            }

            getNextToken();

            return std::make_unique<CallExprAST>(IdName, std::move(Args));
        }

        /// primary
        ///   ::= identifierexpr
        ///   ::= numberexpr
        ///   ::= parenexpr
        static std::unique_ptr<ExprAST> ParsePrimary() {
            switch (CurTok) {
            default:
                return LogError("unknown token when expecting an expression");
            case IdentifierToken:
                return ParseIdentifierExpr();
            case NumberToken:
                return ParseNumberExpr();
            case '(':
                return ParseParenExpr();
            }
        }

        /// binoprhs
        ///   ::= ('+' primary)*
        static std::unique_ptr<ExprAST>
        ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
            while (true) {
                int TokPrec = GetTokPrecedence();

                if (TokPrec < ExprPrec)
                    return LHS;

                int BinOp = CurTok;
                getNextToken();

                auto RHS = ParsePrimary();
                if (!RHS)
                    return nullptr;

                int NextPrec = GetTokPrecedence();
                if (TokPrec < NextPrec) {
                    RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
                    if (!RHS)
                        return nullptr;
                }

                // Merge LHS/RHS.
                LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS),
                                                      std::move(RHS));
            }
        }

        /// expression
        ///   ::= primary binoprhs
        static std::unique_ptr<ExprAST> ParseExpression() {
            auto LHS = ParsePrimary();
            if (!LHS)
                return nullptr;

            return ParseBinOpRHS(0, std::move(LHS));
        }

        /// prototype
        ///   ::= id '(' id* ')'
        static std::unique_ptr<PrototypeAST> ParsePrototype() {
            if (CurTok != IdentifierToken)
                return LogErrorP("Expected function name in prototype");

            std::string FnName = IdentifierStr;
            getNextToken();

            if (CurTok != '(')
                return LogErrorP("Expected '(' in prototype");

            std::vector<std::string> ArgNames;
            while (getNextToken() == IdentifierToken)
                ArgNames.push_back(IdentifierStr);
            if (CurTok != ')')
                return LogErrorP("Expected ')' in prototype");

            // success
            getNextToken();

            return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
        }

        /// definition ::= 'def' prototype expression
        static std::unique_ptr<FunctionAST> ParseDefinition() {
            getNextToken();
            auto Proto = ParsePrototype();
            if (!Proto)
                return nullptr;

            if (auto E = ParseExpression())
                return std::make_unique<FunctionAST>(std::move(Proto),
                                                     std::move(E));
            return nullptr;
        }

        /// toplevelexpr ::= expression
        static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
            if (auto E = ParseExpression()) {
                // Make an anonymous proto.
                auto Proto = std::make_unique<PrototypeAST>(
                    "__anon_expr", std::vector<std::string>());
                return std::make_unique<FunctionAST>(std::move(Proto),
                                                     std::move(E));
            }
            return nullptr;
        }

        /// external ::= 'extern' prototype
        static std::unique_ptr<PrototypeAST> ParseExtern() {
            getNextToken();
            return ParsePrototype();
        }

        static void HandleDefinition() {
            if (ParseDefinition()) {
                fprintf(stderr, "Parsed a function definition.\n");
            } else {
                getNextToken();
            }
        }

        static void HandleExtern() {
            if (ParseExtern()) {
                fprintf(stderr, "Parsed an extern\n");
            } else {
                getNextToken();
            }
        }

        static void HandleTopLevelExpression() {
            if (ParseTopLevelExpr()) {
                fprintf(stderr, "Parsed a top-level expr\n");
            } else {
                getNextToken();
            }
        }
    };

    /// top ::= definition | external | expression | ';'
    static void MainLoop() {}

} // namespace sushi

int main() {
    using namespace sushi;

    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40; // highest.

    std::cout << "> ";
    getNextToken();

    Parser parser = Parser();

    while (true) {
        std::cout << "> ";
        switch (CurTok) {
        case EOFToken:
            return 0;
        case ';':
            getNextToken();
            break;
        case DefToken:
            parser.HandleDefinition();
            break;
        case ExternToken:
            parser.HandleExtern();
            break;
        default:
            parser.HandleTopLevelExpression();
            break;
        }
    }

    return 0;
}
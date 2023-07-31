#include <iostream>
#include <utility>
#include <memory>
#include <vector>
#include <map>

// -----------------------------------=======
//            Lexer
// -----------------------------------=======

enum Token {
    tok_eof = -1,

    // commands
    tok_def = -2,
    tok_extern = -3,

    // primary
    tok_identifier = -4,
    tok_number = -5,
};

static std::string IdentifierStr; // filled in if tok_identifier
static double NumVal; // filled in if tok_number

/// gettok - return the next token from standard input
static int gettok() {
    static int LastChar = ' '; // not sure why this is stored as an int and not a char? -- following LLVM guide will change later?

    // skip any whitespace
    while (isspace(LastChar))
        LastChar = getchar();

    // needs to recognize any command tokens (e.g. def)
    if (isalpha(LastChar)) {
        IdentifierStr = std::to_string(LastChar); // casting is implicit but it is good to specify probably

        while (isalnum((LastChar = getchar())))
            IdentifierStr += std::to_string(LastChar);

        // Check that the token is one of our known identifiers, otherwise, return value for general identifier
        if (IdentifierStr == "def")
            return tok_def;
        if (IdentifierStr == "extern")
            return tok_extern;

        return tok_identifier;
    }

    if (isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;

        do {
            NumStr += std::to_string(LastChar);
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.'); // why does LLVM guide use do while here and not in the above implementation; TODO: Fix later

        NumVal = strtod(NumStr.c_str(), nullptr); // not sure why clangtidy has a problem with this? not really even sure what it does
        return tok_number;
    }

    if (LastChar == '#') {
        // Comment until end of line
        do
            LastChar = getchar();
        while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF)
            return gettok(); // function must always return a value
    }

    // don't eat EOF
    if (LastChar == EOF)
        return tok_eof;

    // char must be an operator (or something like it) return that and then move the buffer
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

// -----------------------------------=======
//            End Lexer
// -----------------------------------=======

// -----------------------------------=======
//            AST
// -----------------------------------=======
namespace { 

class ExprAST {
public:
    virtual ~ExprAST() = default;
};

/// NumberExprAST -- Class for numeric literals (1.0)
class NumberExprAST : public ExprAST {
    double Val;

public:
    NumberExprAST(double Val) : Val(Val) {}
};

/// VariableExprAST -- Class for referencing a variable, like "a"
class VariableExprAST : public ExprAST {
    std::string Name;

public:
    VariableExprAST(const std::string &Name) : Name(Name) {}
};

/// BinaryExprAST -- Expression class for a binary operator .
class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS) : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {} // what TODO: review
};

/// CallExprAST -- Expression class for function calls
class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

public:
    CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args) : Callee(Callee), Args(std::move(Args)) {};
};

/// Prototype AST -- This class represents the prototype for a function,
/// which captures its name, and its argument names (thus implicitly the number of arguments the function takes)
class PrototypeAST {
    std::string Name;
    std::vector<std::string> Args;

public:
    PrototypeAST(const std::string &Name, std::vector<std::string> Args) : Name(Name), Args(std::move(Args)) {};
};

/// FunctionAST -- This class represents a function definition itself.
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body) : Proto(std::move(Proto)), Body(std::move(Body)) {};
};

} // end anonymous namespace

// -----------------------------------=======
//            End AST
// -----------------------------------=======

// -----------------------------------=======
//            Parser
// -----------------------------------=======

/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
static int CurTok;
static int getNextToken() {
    return CurTok = gettok();
}

/// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str){
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
    LogError(Str);
    return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    // When the lexer reads a number it assigns that number into the NumVal variable
    // a NumberExprAST is then and returned
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken(); // consume the number
    return std::move(Result);
}

// Parenthesis do not exist in the AST because they only serve to guide the parser in creating the AST.
// The AST could be built in order to include parenthesis, but it wasn't so deal with it!
/// parenexpr ::= '(' expr ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken(); // eat (.
    auto V = ParseExpression(); // recursion can occur here
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

    getNextToken(); // eat identifier

    // verify the token is not a function call
    if (CurTok!= '(')
        return std::make_unique<VariableExprAST>(IdName);

    // if the next token is ( then this is the beginning of a function call
    getNextToken();
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (CurTok != ')') {
        while (true) {
            // iterate to parse all arguments
            if (auto Arg = ParseExpression())
                Args.push_back(std::move(Arg));
            else
                return nullptr;

            // we've reached the end of the provided argument list
            // ex. Foo(x, y, z) <-- that close paren
            if (CurTok == ')')
                break;

            if (CurTok != ',')
                return LogError("Expected ')' or ',' in argument list");

            getNextToken();
        }
    }

    getNextToken(); // eat ).

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
        case tok_identifier:
            return ParseIdentifierExpr();
        case tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParenExpr();
    }
}

/// BinOpPrecedence -- This holds the precedence for every operator that is defined. Values given in main()
static std::map<char, int> BinOpPrecedence;

/*
 * With the helper above defined, we can now start parsing binary expressions.
 * The basic idea of operator precedence parsing is to break down an expression with potentially ambiguous binary operators into pieces.
 * Consider, for example, the expression “a+b+(c+d)*e*f+g”.
 * Operator precedence parsing considers this as a stream of primary expressions separated by binary operators.
 * As such, it will first parse the leading primary expression “a”, then it will see the pairs [+, b] [+, (c+d)] [*, e] [*, f]
 *      and [+, g]. Note that because parentheses are primary expressions, the binary expression parser
 *      doesn’t need to worry about nested subexpressions like (c+d) at all.
 *
 * - LLVM Documentation on the workings of recursive binary operator parsing
 */

/// GetTokPrecedence -- Get the precedence of a binary operator token
static int GetTokPrecedence() {
    if (!isascii(CurTok)) { // catch any value of CurTok that would not work as a key to BinOpPrecedence
        return -1;
    }

    int TokPrec = BinOpPrecedence[CurTok]; // int --> char is implicit
    if (TokPrec <= 0) return -1;
    return TokPrec;
}

// The precedence value passed into ParseBinOpRHS indicates the minimal operator precedence that the function is allowed to eat.
// For example, if the current pair stream is [+, x] and ParseBinOpRHS is passed in a precedence of 40,
// it will not consume any tokens (because the precedence of ‘+’ is only 20).

/// binoprhs
/// ::=('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {

    // If this is a binop, find its precedence
    while (true) {
        int TokPrec = GetTokPrecedence();

        // If this is a binop that binds at least as tightly as the current
        // binop, then consume it otherwise we are done.
        if (TokPrec < ExprPrec)
            return LHS;

        // Okay, we know this is a binOp
        int BinOp = CurTok;
        getNextToken(); // eat binop

        auto RHS = ParsePrimary();
        if (!RHS)
            return nullptr;

        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS)
                return nullptr;
        }

        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    }
}

/// expression
///   ::= primary binoprhs
///
static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary();

    if (!LHS)
        return nullptr; // parse primary can return nullptr if it does then there is no LHS expr therefore no RHS

    return ParseBinOpRHS(0, std::move(LHS));
}

/// prototype
///   ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    // when this is called extern has just been eaten

    if (CurTok != tok_identifier)
        return LogErrorP("Expected function name in prototype");

    std::string FnName = IdentifierStr;
    getNextToken();

    if (CurTok != '(')
        return LogErrorP("Expected '(' in prototype");

    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);

    // when the while loop ends the final tok should be a ')'.
    if (CurTok != ')')
        return LogErrorP("Expected ')' in prototype");

    // success.
    getNextToken(); // eat ).

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

/// definition ::= 'def' prototype extension
static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken(); // eat def.

    auto Proto = ParsePrototype();
    if (!Proto) return nullptr;

    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));

    return nullptr;
}

static std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken(); // eat extern.
    return ParsePrototype();
}

// evaluate top level expressions TODO: review
/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        // Make anonymous proto
        auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }

    return nullptr;
}

// -----------------------------------=======
//            End Parser
// -----------------------------------=======

// -----------------------------------=======
//            Top Level Parsing
// -----------------------------------=======

static void HandleDefinition() {
    if(ParseDefinition()) {
        fprintf(stderr, "Parsed a function definition.\n");
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleExtern() {
    if (ParseExtern()) {
        fprintf(stderr, "Parsed an extern.\n");
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleTopLevelExpression() {
    // Evaluate a top-level expression into an anonymous function.
    if (ParseTopLevelExpr()) {
        fprintf(stderr, "Parsed a top-level expr\n");
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

/// top ::= definition | external | expression | ';'
static void MainLoop() {
    while (true) {
        fprintf(stderr, "ready> ");
        switch(CurTok) {
            case tok_eof:
                return;
            case ';': // ignore top-level semicolons
                getNextToken();
                break;
            case tok_def:
                HandleDefinition();
                break;
            case tok_extern:
                HandleExtern();
                break;
            default:
                HandleTopLevelExpression();
                break;
        }
    }
}

int main() {
    BinOpPrecedence['<'] = 10;
    BinOpPrecedence['+'] = 20;
    BinOpPrecedence['-'] = 30;
    BinOpPrecedence['*'] = 40;

    // Prime the first token
    fprintf(stderr, "ready> ");
    getNextToken();

    // Run the main "interpreter loop" now.
    MainLoop();

    return 0;
}

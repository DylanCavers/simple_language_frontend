#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <memory>
#include <vector>
#include <map>

/*
Lexer
- Used to break the input up into tokens
*/

enum Token {
    tok_eof = -1,

    // commands
    tok_def = -2,
    tok_extern = -3,

    // primary
    tok_identifier = -4,
    tok_number = -5,
};

static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal; // Filled in if tok_number

static int gettok() {
    static int LastChar = ' ';

    while (isspace(LastChar))
        LastChar = getchar();

    if (isalpha(LastChar)) {
        IdentifierStr = LastChar; //IndentifierStr assigned here and used later in ParseIdentifierExpr()
        while (isalnum((LastChar = getchar())))
            IdentifierStr += LastChar;

        if (IdentifierStr == "def")
            return tok_def;
        if (IdentifierStr == "extern")
            return tok_extern;
        return tok_identifier;
    }

    if (isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), 0);
        return tok_number;
    }

    if (LastChar == '#') {
        do
            LastChar = getchar();
        while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF)
            return gettok();
    }

    if (LastChar == EOF)
        return tok_eof;

    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

class ExprAST {
public:
    virtual ~ExprAST() = default;
    virtual void print() = 0;
};

class NumberExprAST : public ExprAST {
    double Val;

    public: 
    NumberExprAST(double Val) : Val(Val) {}
    virtual void print() {
        printf("NumberExprAST: %f\n", Val);
    }
};

class VariableExprAST : public ExprAST {
    std::string Name;

    public:
    VariableExprAST(const std::string &Name) : Name(Name) {}
    virtual void print() {
        printf("VariableExprAST: %s", Name.c_str());
    }
};

class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

    public:
    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
        : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
    virtual void print() {
        printf("BinaryExprAST: %c ", Op);
    }
};

class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

    public:
    CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
        : Callee(Callee), Args(std::move(Args)) {}
    virtual void print() {
        printf("CallExprAST: %s ", Callee.c_str());
    }
};

class PrototypeAST {
    std::string Name;
    std::vector<std::string> Args;

    public:
    PrototypeAST(const std::string &Name, std::vector<std::string> Args)
        : Name(Name), Args(std::move(Args)) {}

    const std::string &getName() const { return Name; }
    virtual void print() {
        printf("PrototypeAST: %s ", Name.c_str());}
};

class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

    public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
        : Proto(std::move(Proto)), Body(std::move(Body)) {}
    virtual void print() {
        printf("FunctionAST: %s ", Proto.get()->getName().c_str());
    }
};

static int CurTok;
static int getNextToken() {
    return CurTok = gettok();
}

//Error handling
static std::unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "LogError: %s\n", Str);
    return nullptr;
}
static std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
    LogError(Str);
    return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    Result.get()->print();
    getNextToken();
    return std::move(Result);
}

static std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken();
    auto V = ParseExpression();
    if (!V)
        return nullptr;
    
    if (CurTok != ')')
        return LogError("expected ')'");
    getNextToken();
    return V;
}

static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
    std::string IdName = IdentifierStr; //IdentifierStr is assigned in gettok()
    getNextToken();

    //uses lookahead to see if the identifier is a function call
    if (CurTok != '(')
        return std::make_unique<VariableExprAST>(IdName);

    getNextToken();

    std::vector<std::unique_ptr<ExprAST>> Args;
    //while loop used to extract the arguments from within the brackets
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

static std::unique_ptr<ExprAST> ParsePrimary() {
    switch (CurTok) {
        default:
            return LogError("Unknown token");
        case tok_identifier:
            return ParseIdentifierExpr();
        case tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParenExpr();
    }
}

//initalise map for binary operator precedence
static std::map<char, int> BinopPrecedence;

//return the operator precedence of the current token
static int GetTokPrecendence() {
    if (!isascii(CurTok))
        return -1;

    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec<=0) return -1;
    return TokPrec;
}

static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
    while (true) {
        int TokPrec = GetTokPrecendence();
        if (TokPrec < ExprPrec)
            return LHS;

        int BinOp = CurTok;
        getNextToken();

        auto RHS = ParsePrimary();
        if (!RHS)
            return nullptr;

        int NextPrec = GetTokPrecendence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec+1, std::move(RHS));
            if (!RHS)
                return nullptr;
        }
        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    }
}

static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary();
    if (!LHS)
        return nullptr;
    return ParseBinOpRHS(0, std::move(LHS));
}

//add parsing for function prototypes, function definitions, and top level expressions
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    if (CurTok != tok_identifier)
        return LogErrorP("Expected function name in prototype");

    std::string FnName = IdentifierStr;
    getNextToken();

    if (CurTok != '(')
        return LogErrorP("Expected '(' in prototype");

    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);
    if (CurTok != ')')
        return LogErrorP("Expected ')' in prototype");

    getNextToken();

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken();
    auto Proto = ParsePrototype();

    if (!Proto)
        return nullptr;

    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    return nullptr;
}

static std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken(); //eats the extern token
    return ParsePrototype();
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
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

//add driver code
static void MainLoop() {
    while (true) {
        //fprintf(stderr, "ready> ");
        switch (CurTok) {
            case tok_eof:
                return;
            case ';':
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
    //set binary operator precedences -> extend this section
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;

    //fprintf(stderr, "ready> ");
    getNextToken();
    MainLoop();

    return 0;
}
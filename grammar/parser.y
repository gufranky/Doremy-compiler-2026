%{
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "ast.h"

extern int yylex();
extern int yylineno;
extern char* yytext;
extern FILE* yyin;

void yyerror(const char* s);

CompUnit* root = nullptr;  // Global root of AST
%}

/* Union for semantic values */
%union {
    int num;
    std::string* str;
    
    CompUnit* compUnit;
    FuncDef* funcDef;
    Param* param;
    std::vector<Param*>* paramList;
    
    Stmt* stmt;
    Block* block;
    std::vector<Stmt*>* stmtList;
    
    Expr* expr;
    std::vector<Expr*>* exprList;
}

/* Token declarations */
%token <num> NUMBER
%token <str> IDENTIFIER

%token INT VOID
%token IF ELSE WHILE BREAK CONTINUE RETURN
%token CONST
%token PLUS MINUS STAR SLASH PERCENT
%token LT GT LE GE EQ NE
%token AND OR NOT
%token ASSIGN
%token LPAREN RPAREN LBRACE RBRACE SEMICOLON COMMA

/* Non-terminal types */
%type <compUnit> CompUnit
%type <funcDef> FuncDef
%type <num> ReturnType
%type <param> Param
%type <paramList> ParamList ParamListOpt

%type <stmt> Stmt
%type <block> Block
%type <stmtList> StmtList

%type <expr> Expr PrimaryExpr UnaryExpr MulExpr AddExpr RelExpr LAndExpr LOrExpr
%type <exprList> ArgList ArgListOpt
%type <num> UnaryOp

/* Operator precedence and associativity */
%left OR
%left AND
%left EQ NE
%left LT GT LE GE
%left PLUS MINUS
%left STAR SLASH PERCENT
%right NOT UNARY_MINUS UNARY_PLUS

%%

/* Compilation Unit */
CompUnit
    : FuncDef {
        $$ = new CompUnit();
        $$->functions.emplace_back($1);
        root = $$;
    }
    | CompUnit FuncDef {
        $$ = $1;
        $$->functions.emplace_back($2);
    }
    ;

/* Function Definition */
FuncDef
    : ReturnType IDENTIFIER LPAREN ParamListOpt RPAREN Block {
        FuncDef::ReturnType rt = ($1 == 0) ? FuncDef::RT_INT : FuncDef::RT_VOID;
        $$ = new FuncDef(rt, *$2, $6);
        if ($4) {
            for (auto* param : *$4) {
                $$->params.emplace_back(param);
            }
            delete $4;
        }
        delete $2;
    }
    ;

ReturnType
    : INT  { $$ = 0; }
    | VOID { $$ = 1; }
    ;

/* Parameters */
ParamListOpt
    : /* empty */ { $$ = nullptr; }
    | ParamList { $$ = $1; }
    ;

ParamList
    : Param {
        $$ = new std::vector<Param*>();
        $$->push_back($1);
    }
    | ParamList COMMA Param {
        $$ = $1;
        $$->push_back($3);
    }
    ;

Param
    : INT IDENTIFIER {
        $$ = new Param(*$2);
        delete $2;
    }
    ;

/* Block */
Block
    : LBRACE StmtList RBRACE {
        $$ = new Block();
        if ($2) {
            for (auto* stmt : *$2) {
                $$->stmts.emplace_back(stmt);
            }
            delete $2;
        }
    }
    ;

StmtList
    : /* empty */ { $$ = nullptr; }
    | StmtList Stmt {
        if ($1 == nullptr) {
            $$ = new std::vector<Stmt*>();
        } else {
            $$ = $1;
        }
        $$->push_back($2);
    }
    ;

/* Statements */
Stmt
    : Block {
        $$ = $1;
    }
    | SEMICOLON {
        $$ = new EmptyStmt();
    }
    | Expr SEMICOLON {
        $$ = new ExprStmt($1);
    }
    | IDENTIFIER ASSIGN Expr SEMICOLON {
        $$ = new AssignStmt(*$1, $3);
        delete $1;
    }
    | INT IDENTIFIER ASSIGN Expr SEMICOLON {
        $$ = new VarDeclStmt(*$2, $4);
        delete $2;
    }
    | INT IDENTIFIER SEMICOLON {
        $$ = new VarDeclStmt(*$2, new NumberExpr(0));
        delete $2;
    }
    | CONST INT IDENTIFIER ASSIGN Expr SEMICOLON {
        $$ = new VarDeclStmt(*$3, $5);
        delete $3;
    }
    | IF LPAREN Expr RPAREN Stmt {
        $$ = new IfStmt($3, $5);
    }
    | IF LPAREN Expr RPAREN Stmt ELSE Stmt {
        $$ = new IfStmt($3, $5, $7);
    }
    | WHILE LPAREN Expr RPAREN Stmt {
        $$ = new WhileStmt($3, $5);
    }
    | BREAK SEMICOLON {
        $$ = new BreakStmt();
    }
    | CONTINUE SEMICOLON {
        $$ = new ContinueStmt();
    }
    | RETURN Expr SEMICOLON {
        $$ = new ReturnStmt($2);
    }
    | RETURN SEMICOLON {
        $$ = new ReturnStmt(nullptr);
    }
    ;

/* Expressions */
Expr
    : LOrExpr { $$ = $1; }
    ;

LOrExpr
    : LAndExpr { $$ = $1; }
    | LOrExpr OR LAndExpr {
        $$ = new BinaryExpr(BinaryExpr::B_OR, $1, $3);
    }
    ;

LAndExpr
    : RelExpr { $$ = $1; }
    | LAndExpr AND RelExpr {
        $$ = new BinaryExpr(BinaryExpr::B_AND, $1, $3);
    }
    ;

RelExpr
    : AddExpr { $$ = $1; }
    | RelExpr LT AddExpr {
        $$ = new BinaryExpr(BinaryExpr::B_LT, $1, $3);
    }
    | RelExpr GT AddExpr {
        $$ = new BinaryExpr(BinaryExpr::B_GT, $1, $3);
    }
    | RelExpr LE AddExpr {
        $$ = new BinaryExpr(BinaryExpr::B_LE, $1, $3);
    }
    | RelExpr GE AddExpr {
        $$ = new BinaryExpr(BinaryExpr::B_GE, $1, $3);
    }
    | RelExpr EQ AddExpr {
        $$ = new BinaryExpr(BinaryExpr::B_EQ, $1, $3);
    }
    | RelExpr NE AddExpr {
        $$ = new BinaryExpr(BinaryExpr::B_NE, $1, $3);
    }
    ;

AddExpr
    : MulExpr { $$ = $1; }
    | AddExpr PLUS MulExpr {
        $$ = new BinaryExpr(BinaryExpr::B_ADD, $1, $3);
    }
    | AddExpr MINUS MulExpr {
        $$ = new BinaryExpr(BinaryExpr::B_SUB, $1, $3);
    }
    ;

MulExpr
    : UnaryExpr { $$ = $1; }
    | MulExpr STAR UnaryExpr {
        $$ = new BinaryExpr(BinaryExpr::B_MUL, $1, $3);
    }
    | MulExpr SLASH UnaryExpr {
        $$ = new BinaryExpr(BinaryExpr::B_DIV, $1, $3);
    }
    | MulExpr PERCENT UnaryExpr {
        $$ = new BinaryExpr(BinaryExpr::B_MOD, $1, $3);
    }
    ;

UnaryExpr
    : PrimaryExpr { $$ = $1; }
    | UnaryOp UnaryExpr {
        UnaryExpr::OpType op;
        if ($1 == 0) op = UnaryExpr::U_PLUS;
        else if ($1 == 1) op = UnaryExpr::U_MINUS;
        else op = UnaryExpr::U_NOT;
        $$ = new UnaryExpr(op, $2);
    }
    ;

UnaryOp
    : PLUS  { $$ = 0; }
    | MINUS { $$ = 1; }
    | NOT   { $$ = 2; }
    ;

PrimaryExpr
    : IDENTIFIER {
        $$ = new IdentifierExpr(*$1);
        delete $1;
    }
    | NUMBER {
        $$ = new NumberExpr($1);
    }
    | LPAREN Expr RPAREN {
        $$ = new ParenExpr($2);
    }
    | IDENTIFIER LPAREN ArgListOpt RPAREN {
        $$ = new FunctionCallExpr(*$1);
        if ($3) {
            for (auto* arg : *$3) {
                static_cast<FunctionCallExpr*>($$)->args.emplace_back(arg);
            }
            delete $3;
        }
        delete $1;
    }
    ;

/* Function Arguments */
ArgListOpt
    : /* empty */ { $$ = nullptr; }
    | ArgList { $$ = $1; }
    ;

ArgList
    : Expr {
        $$ = new std::vector<Expr*>();
        $$->push_back($1);
    }
    | ArgList COMMA Expr {
        $$ = $1;
        $$->push_back($3);
    }
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Parse error at line %d near '%s': %s\n", yylineno,
            yytext ? yytext : "<eof>", s);
    exit(1);
}

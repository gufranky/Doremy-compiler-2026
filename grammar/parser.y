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

CompUnit* root = nullptr;
%}

%union {
    int num;
    float fnum;
    std::string* str;
    Type* type;

    ASTNode* astNode;
    CompUnit* compUnit;
    FuncDef* funcDef;
    Param* param;
    std::vector<Param*>* paramList;

    Stmt* stmt;
    Block* block;
    std::vector<Stmt*>* stmtList;
    VarDeclStmt* declStmt;
    VarDef* varDef;
    std::vector<VarDef*>* varDefList;

    Expr* expr;
    std::vector<Expr*>* exprList;
}

%token <num> NUMBER
%token <fnum> FLOAT_NUMBER
%token <str> IDENTIFIER

%token INT VOID FLOAT
%token IF ELSE WHILE BREAK CONTINUE RETURN
%token CONST
%token PLUS MINUS STAR SLASH PERCENT
%token LT GT LE GE EQ NE
%token AND OR NOT
%token ASSIGN
%token LPAREN RPAREN LBRACE RBRACE SEMICOLON COMMA

%type <compUnit> CompUnit
%type <astNode> GlobalItem
%type <param> Param
%type <paramList> ParamList ParamListOpt
%type <type> BType

%type <stmt> Decl BlockItem Stmt
%type <block> Block
%type <stmtList> BlockItemList
%type <declStmt> VarDecl ConstDecl
%type <varDef> VarDef ConstDef
%type <varDefList> VarDefList ConstDefList OptMoreVarDefs
%type <expr> Expr ConstExpr PrimaryExpr UnaryExpr MulExpr AddExpr RelExpr EqExpr LAndExpr LOrExpr OptInitExpr
%type <exprList> ArgList ArgListOpt
%type <num> UnaryOp

%left OR
%left AND
%left EQ NE
%left LT GT LE GE
%left PLUS MINUS
%left STAR SLASH PERCENT
%right NOT UNARY_MINUS UNARY_PLUS

%%

CompUnit
    : GlobalItem {
        $$ = new CompUnit();
        $$->items.emplace_back($1);
        root = $$;
    }
    | CompUnit GlobalItem {
        $$ = $1;
        $$->items.emplace_back($2);
    }
    ;

BType
    : INT {
        $$ = new Type(Type::Int());
    }
    | FLOAT {
        $$ = new Type(Type::Float());
    }
    ;


GlobalItem
    : VOID IDENTIFIER LPAREN ParamListOpt RPAREN Block {
        FuncDef* func = new FuncDef(Type::Void(), *$2, $6);
        if ($4) {
            for (auto* param : *$4) {
                func->params.emplace_back(param);
            }
            delete $4;
        }
        delete $2;
        $$ = static_cast<ASTNode*>(func);
    }
    | BType IDENTIFIER LPAREN ParamListOpt RPAREN Block {
        FuncDef* func = new FuncDef(*$1, *$2, $6);
        if ($4) {
            for (auto* param : *$4) {
                func->params.emplace_back(param);
            }
            delete $4;
        }
        delete $1;
        delete $2;
        $$ = static_cast<ASTNode*>(func);
    }
    | BType IDENTIFIER OptInitExpr OptMoreVarDefs SEMICOLON {
        VarDeclStmt* decl = new VarDeclStmt(*$1);
        VarDef* first = $3 ? new VarDef(*$2, $3) : new VarDef(*$2);
        decl->defs.emplace_back(first);
        if ($4) {
            for (auto* def : *$4) {
                decl->defs.emplace_back(def);
            }
            delete $4;
        }
        delete $1;
        delete $2;
        $$ = static_cast<ASTNode*>(decl);
    }
    | ConstDecl {
        $$ = static_cast<ASTNode*>($1);
    }
    ;

OptInitExpr
    : /* empty */ { $$ = nullptr; }
    | ASSIGN Expr { $$ = $2; }
    ;

OptMoreVarDefs
    : /* empty */ { $$ = nullptr; }
    | COMMA VarDefList { $$ = $2; }
    ;

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
    : BType IDENTIFIER {
        $$ = new Param(*$1, *$2);
        delete $1;
        delete $2;
    }
    ;

Decl
    : VarDecl { $$ = $1; }
    | ConstDecl { $$ = $1; }
    ;

VarDecl
    : BType VarDefList SEMICOLON {
        $$ = new VarDeclStmt(*$1);
        for (auto* def : *$2) {
            $$->defs.emplace_back(def);
        }
        delete $1;
        delete $2;
    }
    ;

ConstDecl
    : CONST BType ConstDefList SEMICOLON {
        Type constType = *$2;
        constType.isConst = true;
        $$ = new VarDeclStmt(constType);
        for (auto* def : *$3) {
            $$->defs.emplace_back(def);
        }
        delete $2;
        delete $3;
    }
    ;

VarDefList
    : VarDef {
        $$ = new std::vector<VarDef*>();
        $$->push_back($1);
    }
    | VarDefList COMMA VarDef {
        $$ = $1;
        $$->push_back($3);
    }
    ;

ConstDefList
    : ConstDef {
        $$ = new std::vector<VarDef*>();
        $$->push_back($1);
    }
    | ConstDefList COMMA ConstDef {
        $$ = $1;
        $$->push_back($3);
    }
    ;

VarDef
    : IDENTIFIER {
        $$ = new VarDef(*$1);
        delete $1;
    }
    | IDENTIFIER ASSIGN Expr {
        $$ = new VarDef(*$1, $3);
        delete $1;
    }
    ;

ConstDef
    : IDENTIFIER ASSIGN ConstExpr {
        $$ = new VarDef(*$1, $3);
        $$->initIsConst = true;
        delete $1;
    }
    ;

Block
    : LBRACE BlockItemList RBRACE {
        $$ = new Block();
        if ($2) {
            for (auto* stmt : *$2) {
                $$->stmts.emplace_back(stmt);
            }
            delete $2;
        }
    }
    ;

BlockItemList
    : /* empty */ { $$ = nullptr; }
    | BlockItemList BlockItem {
        if ($1 == nullptr) {
            $$ = new std::vector<Stmt*>();
        } else {
            $$ = $1;
        }
        $$->push_back($2);
    }
    ;

BlockItem
    : Decl { $$ = $1; }
    | Stmt { $$ = $1; }
    ;

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

ConstExpr
    : AddExpr { $$ = $1; }
    ;

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
    : EqExpr { $$ = $1; }
    | LAndExpr AND EqExpr {
        $$ = new BinaryExpr(BinaryExpr::B_AND, $1, $3);
    }
    ;

EqExpr
    : RelExpr { $$ = $1; }
    | EqExpr EQ RelExpr {
        $$ = new BinaryExpr(BinaryExpr::B_EQ, $1, $3);
    }
    | EqExpr NE RelExpr {
        $$ = new BinaryExpr(BinaryExpr::B_NE, $1, $3);
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
    | FLOAT_NUMBER {
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

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
    std::vector<Expr*>* dimList;
    InitList* initList;
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
%token LPAREN RPAREN LBRACE RBRACE LBRACKET RBRACKET SEMICOLON COMMA

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
%type <expr> Expr ConstExpr PrimaryExpr UnaryExpr MulExpr AddExpr RelExpr EqExpr LAndExpr LOrExpr LVal
%type <exprList> ArgList ArgListOpt ArrayDimsExpr
%type <dimList> ArrayDims
%type <initList> InitVal ConstInitVal InitValList ConstInitValList OptInitVal
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
    | BType IDENTIFIER ArrayDims OptInitVal OptMoreVarDefs SEMICOLON {
        VarDeclStmt* decl = new VarDeclStmt(*$1);
        VarDef* first = new VarDef(*$2);
        first->isArray = ($3 != nullptr && !$3->empty());
        if ($3) {
            for (auto* dim : *$3) {
                first->arrayDimExprs.emplace_back(dim);
            }
            delete $3;
        }
        if ($4) {
            first->hasInit = true;
            first->hasInitList = !($4->isScalar);
            if ($4->isScalar && $4->elements.size() == 1) {
                // 标量初始化：提取表达式
                first->initExpr.reset(dynamic_cast<Expr*>($4->elements[0].release()));
            } else {
                // 数组初始化列表
                first->initList.reset($4);
            }
        }
        decl->defs.emplace_back(first);
        if ($5) {
            for (auto* def : *$5) {
                decl->defs.emplace_back(def);
            }
            delete $5;
        }
        delete $1;
        delete $2;
        $$ = static_cast<ASTNode*>(decl);
    }
    | ConstDecl {
        $$ = static_cast<ASTNode*>($1);
    }
    ;

OptInitVal
    : /* empty */ { $$ = nullptr; }
    | ASSIGN InitVal { $$ = $2; }
    ;

ArrayDims
    : /* empty */ { $$ = nullptr; }
    | ArrayDims LBRACKET ConstExpr RBRACKET {
        if ($1 == nullptr) {
            $$ = new std::vector<Expr*>();
        } else {
            $$ = $1;
        }
        $$->push_back($3);
    }
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
    | BType IDENTIFIER LBRACKET RBRACKET {
        $$ = new Param(*$1, *$2);
        $$->type.isArray = true;
        $$->type.firstDimUnsized = true;
        delete $1;
        delete $2;
    }
    | BType IDENTIFIER LBRACKET RBRACKET ArrayDimsExpr {
        $$ = new Param(*$1, *$2);
        $$->type.isArray = true;
        $$->type.firstDimUnsized = true;
        if ($5) {
            for (auto* dim : *$5) {
                $$->arrayDimExprs.emplace_back(dim);
            }
            delete $5;
        }
        delete $1;
        delete $2;
    }
    ;

ArrayDimsExpr
    : LBRACKET Expr RBRACKET {
        $$ = new std::vector<Expr*>();
        $$->push_back($2);
    }
    | ArrayDimsExpr LBRACKET Expr RBRACKET {
        $$ = $1;
        $$->push_back($3);
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
    : IDENTIFIER ArrayDims {
        $$ = new VarDef(*$1);
        $$->isArray = ($2 != nullptr && !$2->empty());
        if ($2) {
            for (auto* dim : *$2) {
                $$->arrayDimExprs.emplace_back(dim);
            }
            delete $2;
        }
        delete $1;
    }
    | LPAREN VOID STAR RPAREN IDENTIFIER ArrayDims {
        $$ = new VarDef(*$5);
        $$->isArray = ($6 != nullptr && !$6->empty());
        if ($6) {
            for (auto* dim : *$6) {
                $$->arrayDimExprs.emplace_back(dim);
            }
            delete $6;
        }
        delete $5;
    }
    | IDENTIFIER ArrayDims ASSIGN InitVal {
        if ($4->isScalar && $4->elements.size() == 1) {
            // 标量初始化：提取表达式
            $$ = new VarDef(*$1, dynamic_cast<Expr*>($4->elements[0].release()));
        } else {
            // 数组初始化列表
            $$ = new VarDef(*$1, $4);
        }
        $$->isArray = ($2 != nullptr && !$2->empty());
        $$->hasInit = true;
        $$->hasInitList = !($4->isScalar);
        if ($2) {
            for (auto* dim : *$2) {
                $$->arrayDimExprs.emplace_back(dim);
            }
            delete $2;
        }
        delete $1;
    }
    | LPAREN VOID STAR RPAREN IDENTIFIER ArrayDims ASSIGN InitVal {
        if ($8->isScalar && $8->elements.size() == 1) {
            $$ = new VarDef(*$5, dynamic_cast<Expr*>($8->elements[0].release()));
        } else {
            $$ = new VarDef(*$5, $8);
        }
        $$->isArray = ($6 != nullptr && !$6->empty());
        $$->hasInit = true;
        $$->hasInitList = !($8->isScalar);
        if ($6) {
            for (auto* dim : *$6) {
                $$->arrayDimExprs.emplace_back(dim);
            }
            delete $6;
        }
        delete $5;
    }
    ;

ConstDef
    : IDENTIFIER ArrayDims ASSIGN ConstInitVal {
        if ($4->isScalar && $4->elements.size() == 1) {
            // 标量初始化：提取表达式
            $$ = new VarDef(*$1, dynamic_cast<Expr*>($4->elements[0].release()));
        } else {
            // 数组初始化列表
            $$ = new VarDef(*$1, $4);
        }
        $$->isArray = ($2 != nullptr && !$2->empty());
        $$->initIsConst = true;
        $$->hasInit = true;
        $$->hasInitList = !($4->isScalar);
        if ($2) {
            for (auto* dim : *$2) {
                $$->arrayDimExprs.emplace_back(dim);
            }
            delete $2;
        }
        delete $1;
    }
    ;

InitVal
    : Expr {
        $$ = new InitList();
        $$->isScalar = true;
        $$->elements.emplace_back($1);
    }
    | LBRACE RBRACE {
        $$ = new InitList();
        $$->isScalar = false;
    }
    | LBRACE InitValList RBRACE {
        $$ = $2;
    }
    ;

InitValList
    : InitVal {
        $$ = new InitList();
        $$->isScalar = false;
        $$->elements.emplace_back($1);
    }
    | InitValList COMMA InitVal {
        $$ = $1;
        $$->elements.emplace_back($3);
    }
    ;

ConstInitVal
    : ConstExpr {
        $$ = new InitList();
        $$->isScalar = true;
        $$->elements.emplace_back($1);
    }
    | LBRACE RBRACE {
        $$ = new InitList();
        $$->isScalar = false;
    }
    | LBRACE ConstInitValList RBRACE {
        $$ = $2;
    }
    ;

ConstInitValList
    : ConstInitVal {
        $$ = new InitList();
        $$->isScalar = false;
        $$->elements.emplace_back($1);
    }
    | ConstInitValList COMMA ConstInitVal {
        $$ = $1;
        $$->elements.emplace_back($3);
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
    | LVal ASSIGN Expr SEMICOLON {
        $$ = new AssignStmt($1, $3);
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

LVal
    : IDENTIFIER {
        $$ = new IdentifierExpr(*$1);
        delete $1;
    }
    | LPAREN VOID STAR RPAREN IDENTIFIER {
        $$ = new IdentifierExpr(*$5);
        delete $5;
    }
    | LVal LBRACKET Expr RBRACKET {
        $$ = new ArrayAccessExpr($1, $3);
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
    : LVal {
        $$ = $1;
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

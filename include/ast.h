#ifndef AST_H
#define AST_H

#include <memory>
#include <string>
#include <vector>

// Forward declarations
class ASTVisitor;

// Base class for all AST nodes
class ASTNode {
 public:
  virtual ~ASTNode() = default;
  virtual void accept(ASTVisitor* visitor) = 0;
};

// Expression nodes
class Expr : public ASTNode {
 public:
  virtual ~Expr() = default;
};

// Primary Expression
class NumberExpr : public Expr {
 public:
  int value;
  NumberExpr(int val) : value(val) {}
  void accept(ASTVisitor* visitor) override;
};

class IdentifierExpr : public Expr {
 public:
  std::string name;
  IdentifierExpr(const std::string& n) : name(n) {}
  void accept(ASTVisitor* visitor) override;
};

class ParenExpr : public Expr {
 public:
  std::unique_ptr<Expr> expr;
  ParenExpr(Expr* e) : expr(e) {}
  void accept(ASTVisitor* visitor) override;
};

// Function Call
class FunctionCallExpr : public Expr {
 public:
  std::string funcName;
  std::vector<std::unique_ptr<Expr>> args;
  FunctionCallExpr(const std::string& name) : funcName(name) {}
  void accept(ASTVisitor* visitor) override;
};

// Unary Expression
class UnaryExpr : public Expr {
 public:
  enum OpType { U_PLUS, U_MINUS, U_NOT };
  OpType op;
  std::unique_ptr<Expr> operand;
  UnaryExpr(OpType o, Expr* e) : op(o), operand(e) {}
  void accept(ASTVisitor* visitor) override;
};  // Binary Expression
class BinaryExpr : public Expr {
 public:
  enum OpType {
    B_ADD,
    B_SUB,
    B_MUL,
    B_DIV,
    B_MOD,
    B_LT,
    B_GT,
    B_LE,
    B_GE,
    B_EQ,
    B_NE,
    B_AND,
    B_OR
  };
  OpType op;
  std::unique_ptr<Expr> left;
  std::unique_ptr<Expr> right;
  BinaryExpr(OpType o, Expr* l, Expr* r) : op(o), left(l), right(r) {}
  void accept(ASTVisitor* visitor) override;
};

// Statement nodes
class Stmt : public ASTNode {
 public:
  virtual ~Stmt() = default;
};

// Block Statement
class Block : public Stmt {
 public:
  std::vector<std::unique_ptr<Stmt>> stmts;
  void accept(ASTVisitor* visitor) override;
};

// Empty Statement
class EmptyStmt : public Stmt {
 public:
  void accept(ASTVisitor* visitor) override;
};

// Expression Statement
class ExprStmt : public Stmt {
 public:
  std::unique_ptr<Expr> expr;
  ExprStmt(Expr* e) : expr(e) {}
  void accept(ASTVisitor* visitor) override;
};

// Assignment Statement
class AssignStmt : public Stmt {
 public:
  std::string varName;
  std::unique_ptr<Expr> value;
  AssignStmt(const std::string& name, Expr* val) : varName(name), value(val) {}
  void accept(ASTVisitor* visitor) override;
};

// Variable Declaration
class VarDeclStmt : public Stmt {
 public:
  std::string varName;
  std::unique_ptr<Expr> initExpr;
  VarDeclStmt(const std::string& name, Expr* init)
      : varName(name), initExpr(init) {}
  void accept(ASTVisitor* visitor) override;
};

// If Statement
class IfStmt : public Stmt {
 public:
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> thenStmt;
  std::unique_ptr<Stmt> elseStmt;
  IfStmt(Expr* cond, Stmt* then, Stmt* els = nullptr)
      : condition(cond), thenStmt(then), elseStmt(els) {}
  void accept(ASTVisitor* visitor) override;
};

// While Statement
class WhileStmt : public Stmt {
 public:
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> body;
  WhileStmt(Expr* cond, Stmt* b) : condition(cond), body(b) {}
  void accept(ASTVisitor* visitor) override;
};

// Break Statement
class BreakStmt : public Stmt {
 public:
  void accept(ASTVisitor* visitor) override;
};

// Continue Statement
class ContinueStmt : public Stmt {
 public:
  void accept(ASTVisitor* visitor) override;
};

// Return Statement
class ReturnStmt : public Stmt {
 public:
  std::unique_ptr<Expr> returnValue;
  // returnValue may be null for `return;` in void functions
  ReturnStmt(Expr* val = nullptr) : returnValue(val) {}
  void accept(ASTVisitor* visitor) override;
};

// Function Parameter
class Param {
 public:
  std::string name;
  Param(const std::string& n) : name(n) {}
};

// Function Definition
class FuncDef : public ASTNode {
 public:
  enum ReturnType { RT_INT, RT_VOID };
  ReturnType returnType;
  std::string name;
  std::vector<std::unique_ptr<Param>> params;
  std::unique_ptr<Block> body;

  FuncDef(ReturnType rt, const std::string& n, Block* b)
      : returnType(rt), name(n), body(b) {}
  void accept(ASTVisitor* visitor) override;
};  // Compilation Unit (root of AST)
class CompUnit : public ASTNode {
 public:
  std::vector<std::unique_ptr<FuncDef>> functions;
  void accept(ASTVisitor* visitor) override;
};

// Visitor interface
class ASTVisitor {
 public:
  virtual ~ASTVisitor() = default;

  virtual void visit(NumberExpr* node) = 0;
  virtual void visit(IdentifierExpr* node) = 0;
  virtual void visit(ParenExpr* node) = 0;
  virtual void visit(FunctionCallExpr* node) = 0;
  virtual void visit(UnaryExpr* node) = 0;
  virtual void visit(BinaryExpr* node) = 0;

  virtual void visit(Block* node) = 0;
  virtual void visit(EmptyStmt* node) = 0;
  virtual void visit(ExprStmt* node) = 0;
  virtual void visit(AssignStmt* node) = 0;
  virtual void visit(VarDeclStmt* node) = 0;
  virtual void visit(IfStmt* node) = 0;
  virtual void visit(WhileStmt* node) = 0;
  virtual void visit(BreakStmt* node) = 0;
  virtual void visit(ContinueStmt* node) = 0;
  virtual void visit(ReturnStmt* node) = 0;

  virtual void visit(FuncDef* node) = 0;
  virtual void visit(CompUnit* node) = 0;
};

#endif  // AST_H

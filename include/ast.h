#ifndef AST_H
#define AST_H

#include <memory>
#include <string>
#include <vector>

class ASTVisitor;

class ASTNode {
 public:
  virtual ~ASTNode() = default;
  virtual void accept(ASTVisitor* visitor) = 0;
};

enum class BaseType { INVALID, INT, VOID, FLOAT };

struct ScalarValue {
  BaseType base = BaseType::INVALID;
  int intValue = 0;
  float floatValue = 0.0f;

  static ScalarValue Int(int value) {
    ScalarValue v;
    v.base = BaseType::INT;
    v.intValue = value;
    v.floatValue = static_cast<float>(value);
    return v;
  }

  static ScalarValue Float(float value) {
    ScalarValue v;
    v.base = BaseType::FLOAT;
    v.intValue = static_cast<int>(value);
    v.floatValue = value;
    return v;
  }

  bool isInt() const { return base == BaseType::INT; }
  bool isFloat() const { return base == BaseType::FLOAT; }
};

struct Type {
  BaseType base = BaseType::INVALID;
  bool isConst = false;
  bool isArray = false;
  std::vector<int> arrayDimensions;
  bool firstDimUnsized = false;

  static Type Invalid() { return Type{}; }
  static Type Int() {
    Type t;
    t.base = BaseType::INT;
    return t;
  }
  static Type ConstInt() {
    Type t = Int();
    t.isConst = true;
    return t;
  }
  static Type Void() {
    Type t;
    t.base = BaseType::VOID;
    return t;
  }
  static Type Float() {
    Type t;
    t.base = BaseType::FLOAT;
    return t;
  }
  static Type ConstFloat() {
    Type t = Float();
    t.isConst = true;
    return t;
  }

  bool isValid() const { return base != BaseType::INVALID; }
  bool isIntScalar() const { return base == BaseType::INT && !isArray; }
  bool isVoidScalar() const { return base == BaseType::VOID && !isArray; }
  bool isFloatScalar() const { return base == BaseType::FLOAT && !isArray; }

  bool equalsIgnoringConst(const Type& other) const {
    return base == other.base && isArray == other.isArray &&
           arrayDimensions == other.arrayDimensions &&
           firstDimUnsized == other.firstDimUnsized;
  }

  Type withoutConst() const {
    Type t = *this;
    t.isConst = false;
    return t;
  }
};

class Expr : public ASTNode {
 public:
  virtual ~Expr() = default;
};

class NumberExpr : public Expr {
 public:
  int value;
  ScalarValue scalarValue;

  explicit NumberExpr(int val) : value(val), scalarValue(ScalarValue::Int(val)) {}
  explicit NumberExpr(float val)
      : value(static_cast<int>(val)), scalarValue(ScalarValue::Float(val)) {}

  bool isIntLiteral() const { return scalarValue.isInt(); }
  bool isFloatLiteral() const { return scalarValue.isFloat(); }
  void accept(ASTVisitor* visitor) override;
};

class IdentifierExpr : public Expr {
 public:
  std::string name;
  explicit IdentifierExpr(const std::string& n) : name(n) {}
  void accept(ASTVisitor* visitor) override;
};

class ParenExpr : public Expr {
 public:
  std::unique_ptr<Expr> expr;
  explicit ParenExpr(Expr* e) : expr(e) {}
  void accept(ASTVisitor* visitor) override;
};

class FunctionCallExpr : public Expr {
 public:
  std::string funcName;
  std::vector<std::unique_ptr<Expr>> args;
  explicit FunctionCallExpr(const std::string& name) : funcName(name) {}
  void accept(ASTVisitor* visitor) override;
};

class UnaryExpr : public Expr {
 public:
  enum OpType { U_PLUS, U_MINUS, U_NOT };
  OpType op;
  std::unique_ptr<Expr> operand;
  UnaryExpr(OpType o, Expr* e) : op(o), operand(e) {}
  void accept(ASTVisitor* visitor) override;
};

class ArrayAccessExpr : public Expr {
 public:
  std::unique_ptr<Expr> array;  // 可以是 IdentifierExpr 或嵌套的 ArrayAccessExpr
  std::unique_ptr<Expr> index;  // 索引表达式

  ArrayAccessExpr(Expr* arr, Expr* idx) : array(arr), index(idx) {}
  void accept(ASTVisitor* visitor) override;
};

class InitList : public ASTNode {
 public:
  std::vector<std::unique_ptr<ASTNode>> elements;  // 可以是 Expr 或 InitList
  bool isScalar = false;  // 标记是否为单一表达式（非列表）

  InitList() = default;
  void accept(ASTVisitor* visitor) override;
};

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

class Stmt : public ASTNode {
 public:
  virtual ~Stmt() = default;
};

class VarDef {
 public:
  std::string name;
  std::unique_ptr<Expr> initExpr;        // 标量初始化
  std::unique_ptr<InitList> initList;    // 数组初始化列表
  bool hasInit = false;
  bool hasInitList = false;              // 区分标量初始化和列表初始化
  bool initIsConst = false;
  int constInitValue = 0;
  ScalarValue typedConstInitValue = ScalarValue::Int(0);
  bool isArray = false;
  std::vector<int> arrayDimensions;
  std::vector<std::unique_ptr<Expr>> arrayDimExprs;  // 维度表达式（语义分析前）
  bool firstDimUnsized = false;

  explicit VarDef(const std::string& n) : name(n) {}
  VarDef(const std::string& n, Expr* init)
      : name(n), initExpr(init), hasInit(true) {}
  VarDef(const std::string& n, InitList* init)
      : name(n), initList(init), hasInit(true), hasInitList(true) {}
};

class Block : public Stmt {
 public:
  std::vector<std::unique_ptr<Stmt>> stmts;
  void accept(ASTVisitor* visitor) override;
};

class EmptyStmt : public Stmt {
 public:
  void accept(ASTVisitor* visitor) override;
};

class ExprStmt : public Stmt {
 public:
  std::unique_ptr<Expr> expr;
  explicit ExprStmt(Expr* e) : expr(e) {}
  void accept(ASTVisitor* visitor) override;
};

class AssignStmt : public Stmt {
 public:
  std::unique_ptr<Expr> lvalue;    // 通用左值表达式（可以是 IdentifierExpr 或 ArrayAccessExpr）
  std::unique_ptr<Expr> value;

  AssignStmt(Expr* lv, Expr* val) : lvalue(lv), value(val) {}
  void accept(ASTVisitor* visitor) override;
};

class VarDeclStmt : public Stmt {
 public:
  Type declaredType;
  std::vector<std::unique_ptr<VarDef>> defs;

  explicit VarDeclStmt(Type t) : declaredType(std::move(t)) {}
  void accept(ASTVisitor* visitor) override;
};

class IfStmt : public Stmt {
 public:
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> thenStmt;
  std::unique_ptr<Stmt> elseStmt;
  IfStmt(Expr* cond, Stmt* thenBranch, Stmt* elseBranch = nullptr)
      : condition(cond), thenStmt(thenBranch), elseStmt(elseBranch) {}
  void accept(ASTVisitor* visitor) override;
};

class WhileStmt : public Stmt {
 public:
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> body;
  WhileStmt(Expr* cond, Stmt* b) : condition(cond), body(b) {}
  void accept(ASTVisitor* visitor) override;
};

class BreakStmt : public Stmt {
 public:
  void accept(ASTVisitor* visitor) override;
};

class ContinueStmt : public Stmt {
 public:
  void accept(ASTVisitor* visitor) override;
};

class ReturnStmt : public Stmt {
 public:
  std::unique_ptr<Expr> returnValue;
  explicit ReturnStmt(Expr* val = nullptr) : returnValue(val) {}
  void accept(ASTVisitor* visitor) override;
};

class Param {
 public:
  Type type;
  std::string name;
  std::vector<std::unique_ptr<Expr>> arrayDimExprs;  // 数组参数的维度表达式

  Param(Type t, const std::string& n) : type(std::move(t)), name(n) {}
};

class FuncDef : public ASTNode {
 public:
  Type returnType;
  std::string name;
  std::vector<std::unique_ptr<Param>> params;
  std::unique_ptr<Block> body;

  FuncDef(Type rt, const std::string& n, Block* b)
      : returnType(std::move(rt)), name(n), body(b) {}
  void accept(ASTVisitor* visitor) override;
};

class CompUnit : public ASTNode {
 public:
  std::vector<std::unique_ptr<ASTNode>> items;
  void accept(ASTVisitor* visitor) override;
};

class ASTVisitor {
 public:
  virtual ~ASTVisitor() = default;

  virtual void visit(NumberExpr* node) = 0;
  virtual void visit(IdentifierExpr* node) = 0;
  virtual void visit(ParenExpr* node) = 0;
  virtual void visit(FunctionCallExpr* node) = 0;
  virtual void visit(UnaryExpr* node) = 0;
  virtual void visit(BinaryExpr* node) = 0;
  virtual void visit(ArrayAccessExpr* node) = 0;
  virtual void visit(InitList* node) = 0;

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

#endif

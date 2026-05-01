#ifndef IR_GENERATOR_H
#define IR_GENERATOR_H

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ast.h"
#include "ir.h"

class IRGenerator {
 public:
  ir::IRProgram generate(CompUnit* root);

 private:
  struct ValueBinding {
    bool isGlobal = false;
    bool isConst = false;
    Type type = Type::Invalid();
    ScalarValue constValue = ScalarValue::Int(0);
    int vreg = -1;
    std::string globalName;
  };

  struct ExprResult {
    Type type = Type::Invalid();
    ir::Operand operand = ir::Operand::Imm(0);
  };

  struct FunctionSignature {
    Type returnType = Type::Invalid();
    std::vector<Type> paramTypes;
  };

  ir::IRProgram program_;
  ir::IRFunction* current_ = nullptr;
  int labelCounter_ = 0;
  std::vector<std::unordered_map<std::string, ValueBinding>> scopes_;
  std::vector<std::pair<std::string, std::string>> loopStack_;
  std::unordered_map<std::string, FunctionSignature> functions_;

  void declareBuiltinFunctions();
  void enterScope();
  void exitScope();
  ValueBinding* lookupBinding(const std::string& name);
  const ValueBinding* lookupBinding(const std::string& name) const;
  ValueBinding& declareLocalValue(const std::string& name, ValueBinding binding);
  ValueBinding& declareGlobalValue(const std::string& name, ValueBinding binding);

  std::string newLabel(const std::string& prefix);
  int newVReg();

  ir::ValueType toIRValueType(const Type& type) const;
  bool isIntType(const Type& type) const;
  bool isFloatType(const Type& type) const;
  bool isNumericType(const Type& type) const;
  bool isRelationalOp(BinaryExpr::OpType op) const;
  Type commonNumericType(const Type& lhs, const Type& rhs) const;
  ir::Operand castOperand(const ir::Operand& operand, const Type& from,
                          const Type& to);
  ExprResult castExprResult(const ExprResult& result, const Type& targetType);

  void emitDecl(VarDeclStmt* decl, bool isGlobal);
  ExprResult genExprResult(Expr* expr);
  ExprResult genExprResult(Expr* expr, std::vector<std::string>* debugNames);
  ir::Operand genExpr(Expr* expr);
  ir::Operand genExpr(Expr* expr, std::vector<std::string>* debugNames);
  ir::Operand genLogical(BinaryExpr* node);
  ir::Operand genNot(UnaryExpr* node);
  void genCond(Expr* expr, const std::string& trueLabel,
               const std::string& falseLabel);
  void genStmt(Stmt* stmt);
  void genBlock(Block* block);
};

#endif

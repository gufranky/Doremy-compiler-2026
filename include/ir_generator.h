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
    int constValue = 0;
    int vreg = -1;
    std::string globalName;
  };

  ir::IRProgram program_;
  ir::IRFunction* current_ = nullptr;
  int labelCounter_ = 0;
  std::vector<std::unordered_map<std::string, ValueBinding>> scopes_;
  std::vector<std::pair<std::string, std::string>> loopStack_;

  void enterScope();
  void exitScope();
  ValueBinding* lookupBinding(const std::string& name);
  const ValueBinding* lookupBinding(const std::string& name) const;
  ValueBinding& declareLocalValue(const std::string& name, ValueBinding binding);
  ValueBinding& declareGlobalValue(const std::string& name, ValueBinding binding);

  std::string newLabel(const std::string& prefix);
  int newVReg();

  void emitDecl(VarDeclStmt* decl, bool isGlobal);
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

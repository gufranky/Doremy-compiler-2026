#ifndef IR_GENERATOR_H
#define IR_GENERATOR_H

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ast.h"
#include "ir.h"

// Translates AST to linear IR (three-address style) with short-circuit logic.
class IRGenerator {
 public:
  // Entry: translate a full compilation unit
  ir::IRProgram generate(CompUnit* root);

 private:
  ir::IRProgram program_;
  ir::IRFunction* current_ = nullptr;
  int labelCounter_ = 0;

  // Scoping for variables -> virtual register id
  std::vector<std::unordered_map<std::string, int>> scopes_;
  // Loop break/continue targets: pair<breakLabel, continueLabel>
  std::vector<std::pair<std::string, std::string>> loopStack_;

  // Scope helpers
  void enterScope();
  void exitScope();
  int lookupVar(const std::string& name) const;
  int declareVar(const std::string& name, int vreg);

  std::string newLabel(const std::string& prefix);
  int newVReg();

  // Generation helpers
  ir::Operand genExpr(Expr* expr);
  ir::Operand genLogical(BinaryExpr* node);
  ir::Operand genNot(UnaryExpr* node);
  void genCond(Expr* expr, const std::string& trueLabel,
               const std::string& falseLabel);
  void genStmt(Stmt* stmt);
  void genBlock(Block* block);
};

#endif  // IR_GENERATOR_H

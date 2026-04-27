#include "ir_generator.h"

#include <cassert>
#include <utility>

using namespace ir;

namespace {
BinaryOp toBinaryOp(BinaryExpr::OpType op) {
  switch (op) {
    case BinaryExpr::B_ADD:
      return BinaryOp::Add;
    case BinaryExpr::B_SUB:
      return BinaryOp::Sub;
    case BinaryExpr::B_MUL:
      return BinaryOp::Mul;
    case BinaryExpr::B_DIV:
      return BinaryOp::Div;
    case BinaryExpr::B_MOD:
      return BinaryOp::Mod;
    case BinaryExpr::B_LT:
      return BinaryOp::Lt;
    case BinaryExpr::B_GT:
      return BinaryOp::Gt;
    case BinaryExpr::B_LE:
      return BinaryOp::Le;
    case BinaryExpr::B_GE:
      return BinaryOp::Ge;
    case BinaryExpr::B_EQ:
      return BinaryOp::Eq;
    case BinaryExpr::B_NE:
      return BinaryOp::Ne;
    case BinaryExpr::B_AND:
    case BinaryExpr::B_OR:
      break;  // handled separately
  }
  // Should not reach here for logical ops
  return BinaryOp::Add;
}
}  // namespace

ir::IRProgram IRGenerator::generate(CompUnit* root) {
  program_ = IRProgram();
  labelCounter_ = 0;

  for (auto& funcPtr : root->functions) {
    FuncDef* func = funcPtr.get();
    program_.functions.emplace_back(func->name);
    current_ = &program_.functions.back();

    scopes_.clear();
    loopStack_.clear();
    enterScope();

    // Declare parameters in order
    for (auto& paramPtr : func->params) {
      int reg = newVReg();
      declareVar(paramPtr->name, reg);
      current_->params.push_back(reg);
    }

    genBlock(func->body.get());

    exitScope();
  }

  current_ = nullptr;
  return std::move(program_);
}

void IRGenerator::enterScope() { scopes_.emplace_back(); }

void IRGenerator::exitScope() {
  assert(!scopes_.empty());
  scopes_.pop_back();
}

int IRGenerator::lookupVar(const std::string& name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) return found->second;
  }
  return -1;
}

int IRGenerator::declareVar(const std::string& name, int vreg) {
  assert(!scopes_.empty());
  scopes_.back()[name] = vreg;
  return vreg;
}

std::string IRGenerator::newLabel(const std::string& prefix) {
  return prefix + std::to_string(labelCounter_++);
}

int IRGenerator::newVReg() { return current_->newVReg(); }

Operand IRGenerator::genExpr(Expr* expr) {
  if (auto num = dynamic_cast<NumberExpr*>(expr)) {
    return Operand::Imm(num->value);
  }
  if (auto id = dynamic_cast<IdentifierExpr*>(expr)) {
    int reg = lookupVar(id->name);
    return Operand::VReg(reg);
  }
  if (auto paren = dynamic_cast<ParenExpr*>(expr)) {
    return genExpr(paren->expr.get());
  }
  if (auto call = dynamic_cast<FunctionCallExpr*>(expr)) {
    std::vector<Operand> args;
    for (auto& a : call->args) {
      args.emplace_back(genExpr(a.get()));
    }
    int dest = newVReg();
    current_->append<CallInst>(dest, call->funcName, std::move(args));
    return Operand::VReg(dest);
  }
  if (auto unary = dynamic_cast<UnaryExpr*>(expr)) {
    switch (unary->op) {
      case UnaryExpr::U_PLUS: {
        return genExpr(unary->operand.get());
      }
      case UnaryExpr::U_MINUS: {
        Operand rhs = genExpr(unary->operand.get());
        int dest = newVReg();
        current_->append<UnaryInst>(UnaryOp::Neg, dest, rhs);
        return Operand::VReg(dest);
      }
      case UnaryExpr::U_NOT: {
        return genNot(unary);
      }
    }
  }
  if (auto binary = dynamic_cast<BinaryExpr*>(expr)) {
    if (binary->op == BinaryExpr::B_AND || binary->op == BinaryExpr::B_OR) {
      return genLogical(binary);
    }
    Operand lhs = genExpr(binary->left.get());
    Operand rhs = genExpr(binary->right.get());
    int dest = newVReg();
    current_->append<BinaryInst>(toBinaryOp(binary->op), dest, lhs, rhs);
    return Operand::VReg(dest);
  }

  // Should not reach
  return Operand::Imm(0);
}

Operand IRGenerator::genNot(UnaryExpr* node) {
  // Generate boolean result with short-circuit
  int dest = newVReg();
  std::string t = newLabel("not_true");
  std::string f = newLabel("not_false");
  std::string end = newLabel("not_end");

  genCond(node->operand.get(), f, t);  // invert

  current_->append<LabelInst>(t);
  current_->append<CopyInst>(dest, Operand::Imm(1));
  current_->append<JumpInst>(end);

  current_->append<LabelInst>(f);
  current_->append<CopyInst>(dest, Operand::Imm(0));

  current_->append<LabelInst>(end);
  return Operand::VReg(dest);
}

Operand IRGenerator::genLogical(BinaryExpr* node) {
  int dest = newVReg();
  std::string l_true = newLabel("logic_true");
  std::string l_false = newLabel("logic_false");
  std::string l_end = newLabel("logic_end");

  genCond(node, l_true, l_false);

  current_->append<LabelInst>(l_true);
  current_->append<CopyInst>(dest, Operand::Imm(1));
  current_->append<JumpInst>(l_end);

  current_->append<LabelInst>(l_false);
  current_->append<CopyInst>(dest, Operand::Imm(0));

  current_->append<LabelInst>(l_end);
  return Operand::VReg(dest);
}

void IRGenerator::genCond(Expr* expr, const std::string& trueLabel,
                          const std::string& falseLabel) {
  if (auto unary = dynamic_cast<UnaryExpr*>(expr)) {
    if (unary->op == UnaryExpr::U_NOT) {
      genCond(unary->operand.get(), falseLabel, trueLabel);
      return;
    }
  }

  if (auto binary = dynamic_cast<BinaryExpr*>(expr)) {
    if (binary->op == BinaryExpr::B_AND) {
      std::string mid = newLabel("land_rhs");
      genCond(binary->left.get(), mid, falseLabel);
      current_->append<LabelInst>(mid);
      genCond(binary->right.get(), trueLabel, falseLabel);
      return;
    }
    if (binary->op == BinaryExpr::B_OR) {
      std::string mid = newLabel("lor_rhs");
      genCond(binary->left.get(), trueLabel, mid);
      current_->append<LabelInst>(mid);
      genCond(binary->right.get(), trueLabel, falseLabel);
      return;
    }
  }

  Operand cond = genExpr(expr);
  current_->append<BranchInst>(cond, trueLabel, falseLabel);
}

void IRGenerator::genStmt(Stmt* stmt) {
  if (auto blk = dynamic_cast<Block*>(stmt)) {
    genBlock(blk);
    return;
  }
  if (auto empty = dynamic_cast<EmptyStmt*>(stmt)) {
    (void)empty;
    return;
  }
  if (auto exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
    genExpr(exprStmt->expr.get());
    return;
  }
  if (auto assign = dynamic_cast<AssignStmt*>(stmt)) {
    Operand val = genExpr(assign->value.get());
    int dest = lookupVar(assign->varName);
    current_->append<CopyInst>(dest, val);
    return;
  }
  if (auto decl = dynamic_cast<VarDeclStmt*>(stmt)) {
    Operand init = genExpr(decl->initExpr.get());
    int dest = declareVar(decl->varName, newVReg());
    current_->append<CopyInst>(dest, init);
    return;
  }
  if (auto ifs = dynamic_cast<IfStmt*>(stmt)) {
    std::string l_true = newLabel("if_true");
    std::string l_false = newLabel("if_false");
    std::string l_end = newLabel("if_end");

    genCond(ifs->condition.get(), l_true, l_false);

    current_->append<LabelInst>(l_true);
    genStmt(ifs->thenStmt.get());
    current_->append<JumpInst>(l_end);

    current_->append<LabelInst>(l_false);
    if (ifs->elseStmt) {
      genStmt(ifs->elseStmt.get());
    }
    current_->append<LabelInst>(l_end);
    return;
  }
  if (auto wh = dynamic_cast<WhileStmt*>(stmt)) {
    std::string l_cond = newLabel("while_cond");
    std::string l_body = newLabel("while_body");
    std::string l_end = newLabel("while_end");

    loopStack_.push_back({l_end, l_cond});

    current_->append<LabelInst>(l_cond);
    genCond(wh->condition.get(), l_body, l_end);

    current_->append<LabelInst>(l_body);
    genStmt(wh->body.get());
    current_->append<JumpInst>(l_cond);

    current_->append<LabelInst>(l_end);

    loopStack_.pop_back();
    return;
  }
  if (auto brk = dynamic_cast<BreakStmt*>(stmt)) {
    (void)brk;
    if (!loopStack_.empty()) {
      current_->append<JumpInst>(loopStack_.back().first);
    }
    return;
  }
  if (auto cont = dynamic_cast<ContinueStmt*>(stmt)) {
    (void)cont;
    if (!loopStack_.empty()) {
      current_->append<JumpInst>(loopStack_.back().second);
    }
    return;
  }
  if (auto ret = dynamic_cast<ReturnStmt*>(stmt)) {
    if (ret->returnValue) {
      Operand val = genExpr(ret->returnValue.get());
      current_->append<ReturnInst>(val);
    } else {
      current_->append<ReturnInst>();
    }
    return;
  }
}

void IRGenerator::genBlock(Block* block) {
  enterScope();
  for (auto& stmt : block->stmts) {
    genStmt(stmt.get());
  }
  exitScope();
}

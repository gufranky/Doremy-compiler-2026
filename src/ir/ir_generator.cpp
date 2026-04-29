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
      break;
  }
  return BinaryOp::Add;
}
}  // namespace

IRProgram IRGenerator::generate(CompUnit* root) {
  program_ = IRProgram();
  labelCounter_ = 0;
  scopes_.clear();
  loopStack_.clear();
  current_ = nullptr;
  enterScope();

  for (auto& item : root->items) {
    if (auto* decl = dynamic_cast<VarDeclStmt*>(item.get())) {
      emitDecl(decl, true);
      continue;
    }
    auto* func = dynamic_cast<FuncDef*>(item.get());
    if (!func) continue;

    program_.functions.emplace_back(func->name);
    current_ = &program_.functions.back();
    loopStack_.clear();
    enterScope();

    for (auto& paramPtr : func->params) {
      int reg = newVReg();
      ValueBinding binding;
      binding.vreg = reg;
      declareLocalValue(paramPtr->name, binding);
      current_->params.push_back(reg);
      if (func->name == "func" &&
          (paramPtr->name == "ib" || paramPtr->name == "yj" ||
           paramPtr->name == "yu")) {
        fprintf(stderr, "[ir-param] %s -> v%d\n", paramPtr->name.c_str(), reg);
      }
      if (func->name == "func" && reg == 209) {
        fprintf(stderr, "[ir-bind] param %s -> v209\n", paramPtr->name.c_str());
      }
    }

    genBlock(func->body.get());
    exitScope();
    current_ = nullptr;
  }

  exitScope();
  return std::move(program_);
}

void IRGenerator::enterScope() { scopes_.emplace_back(); }

void IRGenerator::exitScope() {
  assert(!scopes_.empty());
  scopes_.pop_back();
}

IRGenerator::ValueBinding* IRGenerator::lookupBinding(const std::string& name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) return &found->second;
  }
  return nullptr;
}

const IRGenerator::ValueBinding* IRGenerator::lookupBinding(
    const std::string& name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) return &found->second;
  }
  return nullptr;
}

IRGenerator::ValueBinding& IRGenerator::declareLocalValue(
    const std::string& name, ValueBinding binding) {
  assert(!scopes_.empty());
  auto result = scopes_.back().emplace(name, std::move(binding));
  return result.first->second;
}

IRGenerator::ValueBinding& IRGenerator::declareGlobalValue(
    const std::string& name, ValueBinding binding) {
  assert(!scopes_.empty());
  auto result = scopes_.front().emplace(name, std::move(binding));
  return result.first->second;
}

std::string IRGenerator::newLabel(const std::string& prefix) {
  return prefix + std::to_string(labelCounter_++);
}

int IRGenerator::newVReg() { return current_->newVReg(); }

void IRGenerator::emitDecl(VarDeclStmt* decl, bool isGlobal) {
  for (auto& defPtr : decl->defs) {
    VarDef* def = defPtr.get();
    if (isGlobal) {
      int initValue = def->hasInit && def->initIsConst ? def->constInitValue : 0;
      program_.globals.emplace_back(def->name, initValue, decl->declaredType.isConst);

      ValueBinding binding;
      binding.isGlobal = true;
      binding.isConst = decl->declaredType.isConst;
      binding.globalName = def->name;
      binding.constValue = initValue;
      if (binding.isConst && def->hasInit && def->initIsConst) {
        binding.isConst = true;
      }
      declareGlobalValue(def->name, binding);
      continue;
    }

    ValueBinding binding;
    binding.isConst = decl->declaredType.isConst;
    if (binding.isConst && def->hasInit && def->initIsConst) {
      binding.constValue = def->constInitValue;
    }
    binding.vreg = newVReg();
    if (current_ && current_->name == "func" && binding.vreg == 209) {
      fprintf(stderr, "[ir-bind] local %s -> v209\n", def->name.c_str());
    }
    auto& slot = declareLocalValue(def->name, binding);
    if (def->hasInit) {
      Operand init = genExpr(def->initExpr.get());
      current_->append<CopyInst>(slot.vreg, init);
    }
  }
}

Operand IRGenerator::genExpr(Expr* expr) { return genExpr(expr, nullptr); }

Operand IRGenerator::genExpr(Expr* expr, std::vector<std::string>* debugNames) {
  if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
    return Operand::Imm(num->value);
  }
  if (auto* id = dynamic_cast<IdentifierExpr*>(expr)) {
    ValueBinding* binding = lookupBinding(id->name);
    if (!binding) return Operand::Imm(0);
    if (debugNames) debugNames->push_back(id->name);
    if (current_ && current_->name == "func" && binding->vreg == 209) {
      fprintf(stderr, "[ir-use] %s reads v209\n", id->name.c_str());
    }
    if (binding->isConst && !binding->isGlobal) {
      return Operand::Imm(binding->constValue);
    }
    if (binding->isGlobal) {
      if (binding->isConst) {
        return Operand::Imm(binding->constValue);
      }
      int dest = newVReg();
      current_->append<LoadInst>(dest, Operand::Global(binding->globalName));
      return Operand::VReg(dest);
    }
    return Operand::VReg(binding->vreg);
  }
  if (auto* paren = dynamic_cast<ParenExpr*>(expr)) {
    return genExpr(paren->expr.get());
  }
  if (auto* call = dynamic_cast<FunctionCallExpr*>(expr)) {
    std::vector<Operand> args;
    for (auto& a : call->args) {
      args.emplace_back(genExpr(a.get()));
    }
    int dest = newVReg();
    current_->append<CallInst>(dest, call->funcName, std::move(args));
    return Operand::VReg(dest);
  }
  if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
    switch (unary->op) {
      case UnaryExpr::U_PLUS:
        return genExpr(unary->operand.get());
      case UnaryExpr::U_MINUS: {
        Operand rhs = genExpr(unary->operand.get());
        int dest = newVReg();
        current_->append<UnaryInst>(UnaryOp::Neg, dest, rhs);
        return Operand::VReg(dest);
      }
      case UnaryExpr::U_NOT:
        return genNot(unary);
    }
  }
  if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
    if (binary->op == BinaryExpr::B_AND || binary->op == BinaryExpr::B_OR) {
      return genLogical(binary);
    }
    Operand lhs = genExpr(binary->left.get());
    Operand rhs = genExpr(binary->right.get());
    int dest = newVReg();
    current_->append<BinaryInst>(toBinaryOp(binary->op), dest, lhs, rhs);
    return Operand::VReg(dest);
  }
  return Operand::Imm(0);
}

Operand IRGenerator::genNot(UnaryExpr* node) {
  int dest = newVReg();
  std::string t = newLabel("not_true");
  std::string f = newLabel("not_false");
  std::string end = newLabel("not_end");

  genCond(node->operand.get(), f, t);

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
  std::string lTrue = newLabel("logic_true");
  std::string lFalse = newLabel("logic_false");
  std::string lEnd = newLabel("logic_end");

  genCond(node, lTrue, lFalse);

  current_->append<LabelInst>(lTrue);
  current_->append<CopyInst>(dest, Operand::Imm(1));
  current_->append<JumpInst>(lEnd);

  current_->append<LabelInst>(lFalse);
  current_->append<CopyInst>(dest, Operand::Imm(0));
  current_->append<LabelInst>(lEnd);
  return Operand::VReg(dest);
}

void IRGenerator::genCond(Expr* expr, const std::string& trueLabel,
                          const std::string& falseLabel) {
  if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
    if (unary->op == UnaryExpr::U_NOT) {
      genCond(unary->operand.get(), falseLabel, trueLabel);
      return;
    }
  }
  if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
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
  if (auto* blk = dynamic_cast<Block*>(stmt)) {
    genBlock(blk);
    return;
  }
  if (dynamic_cast<EmptyStmt*>(stmt)) {
    return;
  }
  if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
    genExpr(exprStmt->expr.get());
    return;
  }
  if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
    Operand val = genExpr(assign->value.get());
    ValueBinding* binding = lookupBinding(assign->varName);
    if (!binding) return;
    if (current_ && current_->name == "func" && binding->vreg == 209) {
      fprintf(stderr, "[ir-assign] %s writes v209\n", assign->varName.c_str());
    }
    if (binding->isGlobal) {
      current_->append<StoreInst>(val, Operand::Global(binding->globalName));
    } else {
      current_->append<CopyInst>(binding->vreg, val);
      if (binding->isConst && val.isImm()) {
        binding->constValue = val.immValue;
      }
    }
    return;
  }
  if (auto* decl = dynamic_cast<VarDeclStmt*>(stmt)) {
    emitDecl(decl, false);
    return;
  }
  if (auto* ifs = dynamic_cast<IfStmt*>(stmt)) {
    std::string lTrue = newLabel("if_true");
    std::string lFalse = newLabel("if_false");
    std::string lEnd = newLabel("if_end");
    genCond(ifs->condition.get(), lTrue, lFalse);
    current_->append<LabelInst>(lTrue);
    genStmt(ifs->thenStmt.get());
    current_->append<JumpInst>(lEnd);
    current_->append<LabelInst>(lFalse);
    if (ifs->elseStmt) genStmt(ifs->elseStmt.get());
    current_->append<LabelInst>(lEnd);
    return;
  }
  if (auto* wh = dynamic_cast<WhileStmt*>(stmt)) {
    std::string lCond = newLabel("while_cond");
    std::string lBody = newLabel("while_body");
    std::string lEnd = newLabel("while_end");
    loopStack_.push_back({lEnd, lCond});
    current_->append<LabelInst>(lCond);
    genCond(wh->condition.get(), lBody, lEnd);
    current_->append<LabelInst>(lBody);
    genStmt(wh->body.get());
    current_->append<JumpInst>(lCond);
    current_->append<LabelInst>(lEnd);
    loopStack_.pop_back();
    return;
  }
  if (dynamic_cast<BreakStmt*>(stmt)) {
    if (!loopStack_.empty()) current_->append<JumpInst>(loopStack_.back().first);
    return;
  }
  if (dynamic_cast<ContinueStmt*>(stmt)) {
    if (!loopStack_.empty()) current_->append<JumpInst>(loopStack_.back().second);
    return;
  }
  if (auto* ret = dynamic_cast<ReturnStmt*>(stmt)) {
    if (ret->returnValue) {
      std::vector<std::string> debugNames;
      Operand retValue = genExpr(ret->returnValue.get(), &debugNames);
      current_->append<ReturnInst>(retValue);
      if (current_ && current_->name == "func" && !debugNames.empty()) {
        fprintf(stderr, "[ir-debug] func return identifiers=%zu\n",
                debugNames.size());
        for (const auto& name : debugNames) {
          if (name == "ib" || name == "yj" || name == "yu") {
            ValueBinding* binding = lookupBinding(name);
            if (binding) {
              fprintf(stderr, "[ir-debug] %s -> v%d\n", name.c_str(),
                      binding->vreg);
            }
          }
        }
      }
    } else {
      current_->append<ReturnInst>();
    }
  }
}

void IRGenerator::genBlock(Block* block) {
  enterScope();
  for (auto& stmt : block->stmts) {
    genStmt(stmt.get());
  }
  exitScope();
}

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

void IRGenerator::declareBuiltinFunctions() {
  functions_["getint"] = FunctionSignature{Type::Int(), {}};
  functions_["getch"] = FunctionSignature{Type::Int(), {}};
  functions_["getfloat"] = FunctionSignature{Type::Float(), {}};

  functions_["putint"] = FunctionSignature{Type::Void(), {Type::Int()}};
  functions_["putch"] = FunctionSignature{Type::Void(), {Type::Int()}};
  functions_["putfloat"] = FunctionSignature{Type::Void(), {Type::Float()}};
}

IRProgram IRGenerator::generate(CompUnit* root) {
  program_ = IRProgram();
  current_ = nullptr;
  labelCounter_ = 0;
  scopes_.clear();
  loopStack_.clear();
  functions_.clear();
  declareBuiltinFunctions();
  enterScope();

  for (auto& item : root->items) {
    auto* func = dynamic_cast<FuncDef*>(item.get());
    if (!func) continue;
    FunctionSignature sig;
    sig.returnType = func->returnType;
    for (auto& param : func->params) {
      sig.paramTypes.push_back(param->type);
    }
    functions_[func->name] = sig;
  }

  for (auto& item : root->items) {
    if (auto* decl = dynamic_cast<VarDeclStmt*>(item.get())) {
      emitDecl(decl, true);
      continue;
    }
    auto* func = dynamic_cast<FuncDef*>(item.get());
    if (!func) continue;

    program_.functions.emplace_back(func->name);
    current_ = &program_.functions.back();
    current_->returnType = toIRValueType(func->returnType);
    loopStack_.clear();
    enterScope();

    for (auto& paramPtr : func->params) {
      int reg = newVReg();
      ValueBinding binding;
      binding.type = paramPtr->type;
      binding.vreg = reg;
      declareLocalValue(paramPtr->name, binding);
      current_->params.push_back(reg);
      current_->paramTypes.push_back(toIRValueType(paramPtr->type));
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

ValueType IRGenerator::toIRValueType(const Type& type) const {
  return isFloatType(type) ? ValueType::F32 : ValueType::I32;
}

bool IRGenerator::isIntType(const Type& type) const {
  return type.base == BaseType::INT && !type.isArray;
}

bool IRGenerator::isFloatType(const Type& type) const {
  return type.base == BaseType::FLOAT && !type.isArray;
}

bool IRGenerator::isNumericType(const Type& type) const {
  return isIntType(type) || isFloatType(type);
}

bool IRGenerator::isRelationalOp(BinaryExpr::OpType op) const {
  switch (op) {
    case BinaryExpr::B_LT:
    case BinaryExpr::B_GT:
    case BinaryExpr::B_LE:
    case BinaryExpr::B_GE:
    case BinaryExpr::B_EQ:
    case BinaryExpr::B_NE:
      return true;
    default:
      return false;
  }
}

Type IRGenerator::commonNumericType(const Type& lhs, const Type& rhs) const {
  if (isFloatType(lhs) || isFloatType(rhs)) return Type::Float();
  return Type::Int();
}

Operand IRGenerator::castOperand(const Operand& operand, const Type& from,
                                 const Type& to) {
  if (!from.isValid() || !to.isValid() || from.equalsIgnoringConst(to)) {
    return operand;
  }
  if (!isNumericType(from) || !isNumericType(to)) return operand;

  if (operand.isImm()) {
    ScalarValue value = operand.scalarValue();
    if (isFloatType(to)) {
      return value.isFloat() ? operand
                             : Operand::Imm(static_cast<float>(value.intValue));
    }
    return value.isFloat() ? Operand::Imm(static_cast<int>(value.floatValue))
                           : operand;
  }

  int dest = newVReg();
  current_->append<CopyInst>(toIRValueType(to), dest, operand);
  return Operand::VReg(dest, toIRValueType(to));
}

IRGenerator::ExprResult IRGenerator::castExprResult(const ExprResult& result,
                                                    const Type& targetType) {
  ExprResult casted = result;
  casted.operand = castOperand(result.operand, result.type, targetType);
  casted.type = targetType.withoutConst();
  return casted;
}

void IRGenerator::emitDecl(VarDeclStmt* decl, bool isGlobal) {
  for (auto& defPtr : decl->defs) {
    VarDef* def = defPtr.get();
    if (isGlobal) {
      ScalarValue initValue =
          def->hasInit && def->initIsConst ? def->typedConstInitValue
                                           : (isFloatType(decl->declaredType)
                                                  ? ScalarValue::Float(0.0f)
                                                  : ScalarValue::Int(0));
      program_.globals.emplace_back(def->name, initValue,
                                    decl->declaredType.isConst);

      ValueBinding binding;
      binding.isGlobal = true;
      binding.isConst = decl->declaredType.isConst && def->hasInit && def->initIsConst;
      binding.type = decl->declaredType;
      binding.globalName = def->name;
      binding.constValue = initValue;
      declareGlobalValue(def->name, binding);
      continue;
    }

    ValueBinding binding;
    binding.isConst = decl->declaredType.isConst;
    binding.type = decl->declaredType;
    if (binding.isConst && def->hasInit && def->initIsConst) {
      binding.constValue = def->typedConstInitValue;
    }
    binding.vreg = newVReg();
    auto& slot = declareLocalValue(def->name, binding);
    if (def->hasInit) {
      ExprResult init = castExprResult(genExprResult(def->initExpr.get()),
                                       decl->declaredType.withoutConst());
      current_->append<CopyInst>(toIRValueType(slot.type), slot.vreg, init.operand);
    }
  }
}

Operand IRGenerator::genExpr(Expr* expr) { return genExpr(expr, nullptr); }

Operand IRGenerator::genExpr(Expr* expr, std::vector<std::string>* debugNames) {
  return genExprResult(expr, debugNames).operand;
}

IRGenerator::ExprResult IRGenerator::genExprResult(Expr* expr) {
  return genExprResult(expr, nullptr);
}

IRGenerator::ExprResult IRGenerator::genExprResult(
    Expr* expr, std::vector<std::string>* debugNames) {
  if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
    ExprResult result;
    result.type = num->isFloatLiteral() ? Type::Float() : Type::Int();
    result.operand = Operand::Imm(num->scalarValue);
    return result;
  }
  if (auto* id = dynamic_cast<IdentifierExpr*>(expr)) {
    ValueBinding* binding = lookupBinding(id->name);
    if (!binding) {
      return ExprResult{Type::Int(), Operand::Imm(0)};
    }
    if (debugNames) debugNames->push_back(id->name);
    if (binding->isConst && !binding->isGlobal) {
      return ExprResult{binding->type.withoutConst(), Operand::Imm(binding->constValue)};
    }
    if (binding->isGlobal) {
      if (binding->isConst) {
        return ExprResult{binding->type.withoutConst(), Operand::Imm(binding->constValue)};
      }
      int dest = newVReg();
      current_->append<LoadInst>(toIRValueType(binding->type), dest,
                                 Operand::Global(binding->globalName,
                                                 toIRValueType(binding->type)));
      return ExprResult{binding->type.withoutConst(),
                        Operand::VReg(dest, toIRValueType(binding->type))};
    }
    return ExprResult{binding->type.withoutConst(),
                      Operand::VReg(binding->vreg, toIRValueType(binding->type))};
  }
  if (auto* paren = dynamic_cast<ParenExpr*>(expr)) {
    return genExprResult(paren->expr.get(), debugNames);
  }
  if (auto* call = dynamic_cast<FunctionCallExpr*>(expr)) {
    std::vector<Operand> args;
    std::vector<ValueType> argTypes;
    Type resultType = Type::Int();
    auto sigIt = functions_.find(call->funcName);
    if (sigIt != functions_.end()) resultType = sigIt->second.returnType;

    for (size_t i = 0; i < call->args.size(); ++i) {
      ExprResult arg = genExprResult(call->args[i].get());
      if (sigIt != functions_.end() && i < sigIt->second.paramTypes.size()) {
        arg = castExprResult(arg, sigIt->second.paramTypes[i].withoutConst());
      }
      args.emplace_back(arg.operand);
      argTypes.push_back(toIRValueType(arg.type));
    }
    int dest = newVReg();
    ValueType irResultType = toIRValueType(resultType);
    current_->append<CallInst>(irResultType, dest, call->funcName, std::move(args),
                               std::move(argTypes));
    return ExprResult{resultType.withoutConst(), Operand::VReg(dest, irResultType)};
  }
  if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
    ExprResult operand = genExprResult(unary->operand.get(), debugNames);
    switch (unary->op) {
      case UnaryExpr::U_PLUS:
        return operand;
      case UnaryExpr::U_MINUS: {
        int dest = newVReg();
        ValueType type = toIRValueType(operand.type);
        current_->append<UnaryInst>(UnaryOp::Neg, type, type, dest, operand.operand);
        return ExprResult{operand.type.withoutConst(), Operand::VReg(dest, type)};
      }
      case UnaryExpr::U_NOT:
        return ExprResult{Type::Int(), genNot(unary)};
    }
  }
  if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
    if (binary->op == BinaryExpr::B_AND || binary->op == BinaryExpr::B_OR) {
      return ExprResult{Type::Int(), genLogical(binary)};
    }

    ExprResult lhs = genExprResult(binary->left.get(), debugNames);
    ExprResult rhs = genExprResult(binary->right.get(), debugNames);

    Type operandType = commonNumericType(lhs.type, rhs.type);
    lhs = castExprResult(lhs, operandType);
    rhs = castExprResult(rhs, operandType);

    Type resultType = isRelationalOp(binary->op) ? Type::Int() : operandType;
    int dest = newVReg();
    current_->append<BinaryInst>(toBinaryOp(binary->op), toIRValueType(operandType),
                                 toIRValueType(resultType), dest, lhs.operand,
                                 rhs.operand);
    return ExprResult{resultType, Operand::VReg(dest, toIRValueType(resultType))};
  }
  return ExprResult{Type::Int(), Operand::Imm(0)};
}

Operand IRGenerator::genNot(UnaryExpr* node) {
  int dest = newVReg();
  std::string t = newLabel("not_true");
  std::string f = newLabel("not_false");
  std::string end = newLabel("not_end");

  genCond(node->operand.get(), f, t);

  current_->append<LabelInst>(t);
  current_->append<CopyInst>(ValueType::I32, dest, Operand::Imm(1));
  current_->append<JumpInst>(end);

  current_->append<LabelInst>(f);
  current_->append<CopyInst>(ValueType::I32, dest, Operand::Imm(0));
  current_->append<LabelInst>(end);
  return Operand::VReg(dest, ValueType::I32);
}

Operand IRGenerator::genLogical(BinaryExpr* node) {
  int dest = newVReg();
  std::string lTrue = newLabel("logic_true");
  std::string lFalse = newLabel("logic_false");
  std::string lEnd = newLabel("logic_end");

  genCond(node, lTrue, lFalse);

  current_->append<LabelInst>(lTrue);
  current_->append<CopyInst>(ValueType::I32, dest, Operand::Imm(1));
  current_->append<JumpInst>(lEnd);

  current_->append<LabelInst>(lFalse);
  current_->append<CopyInst>(ValueType::I32, dest, Operand::Imm(0));
  current_->append<LabelInst>(lEnd);
  return Operand::VReg(dest, ValueType::I32);
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
    ValueBinding* binding = lookupBinding(assign->varName);
    if (!binding) return;
    ExprResult val = castExprResult(genExprResult(assign->value.get()),
                                    binding->type.withoutConst());
    if (binding->isGlobal) {
      current_->append<StoreInst>(toIRValueType(binding->type), val.operand,
                                  Operand::Global(binding->globalName,
                                                  toIRValueType(binding->type)));
    } else {
      current_->append<CopyInst>(toIRValueType(binding->type), binding->vreg,
                                 val.operand);
      if (binding->isConst && val.operand.isImm()) {
        binding->constValue = val.operand.scalarValue();
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
      ExprResult retValue = genExprResult(ret->returnValue.get());
      Type targetType = current_->returnType == ValueType::F32 ? Type::Float() : Type::Int();
      retValue = castExprResult(retValue, targetType);
      current_->append<ReturnInst>(current_->returnType, retValue.operand);
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

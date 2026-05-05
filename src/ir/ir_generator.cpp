#include "ir_generator.h"

#include <cassert>
#include <functional>
#include <iostream>
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
    stackOffset_ = 0;  // 重置栈偏移
    enterScope();

    for (auto& paramPtr : func->params) {
      int reg = newVReg();
      ValueBinding binding;
      binding.type = paramPtr->type;
      binding.isArray = paramPtr->type.isArray;
      binding.isArrayParam = paramPtr->type.isArray;  // 标记数组参数
      binding.arrayDimensions = paramPtr->type.arrayDimensions;
      if (paramPtr->type.isArray) {
        // 数组参数：vreg 存储数组首地址
        binding.vreg = reg;
      } else {
        binding.vreg = reg;
      }
      declareLocalValue(paramPtr->name, binding);
      current_->params.push_back(reg);
      current_->paramTypes.push_back(toIRValueType(paramPtr->type));
    }

    genBlock(func->body.get());
    current_->localArraySize = stackOffset_;  // 保存局部数组所需的栈空间（正数）
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

    // 处理数组声明
    if (def->isArray) {
      // 使用语义分析器计算好的维度来计算数组大小
      int arraySize = 1;
      for (int dim : def->arrayDimensions) {
        arraySize *= dim;
      }
      bool zeroInitialized = isZeroInitializedArray(def);

      if (isGlobal) {
        // 全局数组
        ValueBinding binding;
        binding.isGlobal = true;
        binding.isConst = decl->declaredType.isConst;
        binding.type = decl->declaredType;
        binding.type.isArray = true;
        binding.isArray = true;
        binding.globalName = def->name;
        // 使用语义分析器计算好的维度
        binding.arrayDimensions = def->arrayDimensions;
        binding.type.arrayDimensions = binding.arrayDimensions;
        declareGlobalValue(def->name, binding);

        // 收集初始化值
        std::vector<ScalarValue> initValues;
        if (!zeroInitialized && def->hasInitList && def->initList) {
          // 从初始化列表收集值，使用维度信息
          flattenInitListWithDims(def->initList.get(), initValues, binding.arrayDimensions);
          // 如果初始化值不够，用0填充
          while (initValues.size() < static_cast<size_t>(arraySize)) {
            initValues.push_back(ScalarValue::Int(0));
          }
        }

        // 添加全局数组
        program_.globalArrays.emplace_back(
            def->name,
            binding.arrayDimensions,
            isFloatType(decl->declaredType) ? ValueType::F32 : ValueType::I32,
            std::move(initValues),
            decl->declaredType.isConst);
      } else {
        // 局部数组：在栈上分配空间
        int totalBytes = arraySize * 4;  // 假设每个元素4字节

        ValueBinding binding;
        binding.isConst = decl->declaredType.isConst;
        binding.type = decl->declaredType;
        binding.type.isArray = true;
        binding.isArray = true;
        // 记录数组在局部变量区内的偏移（从0开始）
        binding.stackOffset = stackOffset_;  // 当前累计偏移
        // 使用语义分析器计算好的维度
        binding.arrayDimensions = def->arrayDimensions;
        binding.type.arrayDimensions = binding.arrayDimensions;
        binding.vreg = newVReg();  // 用于存储数组基地址
        declareLocalValue(def->name, binding);

        // 生成局部数组初始化代码
        if (zeroInitialized) {
          int idxReg = newVReg();
          int limitReg = newVReg();
          int addrReg = newVReg();
          int valueReg = newVReg();
          int condReg = newVReg();
          std::string loopLabel = newLabel("arr_zero_loop");
          std::string bodyLabel = newLabel("arr_zero_body");
          std::string endLabel = newLabel("arr_zero_end");

          current_->append<CopyInst>(ValueType::I32, idxReg, Operand::Imm(0));
          current_->append<CopyInst>(ValueType::I32, limitReg, Operand::Imm(totalBytes));
          current_->append<CopyInst>(ValueType::I32, valueReg, Operand::Imm(0));
          current_->append<LabelInst>(loopLabel);
          current_->append<BinaryInst>(BinaryOp::Lt, ValueType::I32, ValueType::I32,
                                       condReg, Operand::VReg(idxReg, ValueType::I32),
                                       Operand::VReg(limitReg, ValueType::I32));
          current_->append<BranchInst>(Operand::VReg(condReg, ValueType::I32), bodyLabel,
                                       endLabel);
          current_->append<LabelInst>(bodyLabel);
          current_->append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32,
                                       addrReg, Operand::LocalVarAddr(binding.stackOffset),
                                       Operand::VReg(idxReg, ValueType::I32));
          current_->append<StoreInst>(Operand::VReg(valueReg, ValueType::I32),
                                      Operand::VReg(addrReg, ValueType::I32));
          current_->append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32,
                                       idxReg, Operand::VReg(idxReg, ValueType::I32),
                                       Operand::Imm(4));
          current_->append<JumpInst>(loopLabel);
          current_->append<LabelInst>(endLabel);
        } else {
          // 首先将整个数组初始化为 0
          for (int i = 0; i < arraySize; ++i) {
            int offset = binding.stackOffset + i * 4;
            Operand addrOp = Operand::LocalVarAddr(offset);
            int valReg = newVReg();
            current_->append<CopyInst>(ValueType::I32, valReg, Operand::Imm(0));
            current_->append<StoreInst>(Operand::VReg(valReg, ValueType::I32), addrOp);
          }
        }

        // 然后存储初始化列表中的值
        if (!zeroInitialized && def->hasInitList && def->initList) {
          std::vector<InitElement> initElements;
          collectInitElements(def->initList.get(), initElements, binding.arrayDimensions);

          // 逐个元素存储初始值
          for (size_t i = 0; i < initElements.size() && i < static_cast<size_t>(arraySize); ++i) {
            // 计算地址
            int offset = binding.stackOffset + static_cast<int>(i) * 4;
            Operand addrOp = Operand::LocalVarAddr(offset);

            // 存储值
            if (initElements[i].isConst) {
              if (initElements[i].constValue.isFloat()) {
                // float 类型
                int valReg = newVReg();
                current_->append<CopyInst>(ValueType::F32, valReg, Operand::Imm(initElements[i].constValue));
                current_->append<StoreInst>(Operand::VReg(valReg, ValueType::F32), addrOp);
              } else if (initElements[i].constValue.intValue != 0) {
                // 非 0 的 int 值才需要存储（0 已经在上面初始化过了）
                int valReg = newVReg();
                current_->append<CopyInst>(ValueType::I32, valReg, Operand::Imm(initElements[i].constValue.intValue));
                current_->append<StoreInst>(Operand::VReg(valReg, ValueType::I32), addrOp);
              }
            } else if (initElements[i].expr) {
              // 非常量表达式，在运行时计算
              ExprResult exprResult = genExprResult(initElements[i].expr);
              current_->append<StoreInst>(toIRValueType(exprResult.type), exprResult.operand, addrOp);
            }
          }
        }

        stackOffset_ += totalBytes;  // 累计栈空间
      }
      continue;
    }

    // 标量变量处理
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
    if (def->hasInit && def->initExpr) {
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

    if (binding->isArray) {
      ExprResult result;
      result.type = binding->type.withoutConst();
      result.isArrayAccess = true;
      if (binding->isGlobal) {
        result.operand = Operand::Global(binding->globalName, ValueType::I32);
        result.addrOperand = Operand::Global(binding->globalName, ValueType::I32);
      } else if (binding->isArrayParam) {
        result.operand = Operand::VReg(binding->vreg, ValueType::I32);
        result.addrOperand = Operand::VReg(binding->vreg, ValueType::I32);
      } else {
        int addrReg = newVReg();
        current_->append<CopyInst>(ValueType::I32, addrReg,
                                   Operand::LocalVarAddr(binding->stackOffset));
        result.operand = Operand::VReg(addrReg, ValueType::I32);
        result.addrOperand = Operand::LocalVarAddr(binding->stackOffset);
      }
      return result;
    }

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
  if (auto* arrAccess = dynamic_cast<ArrayAccessExpr*>(expr)) {
    return genArrayAccess(arrAccess);
  }
  if (auto* call = dynamic_cast<FunctionCallExpr*>(expr)) {
    std::vector<Operand> args;
    std::vector<ValueType> argTypes;
    Type resultType = Type::Int();
    auto sigIt = functions_.find(call->funcName);
    if (sigIt != functions_.end()) resultType = sigIt->second.returnType;

    for (size_t i = 0; i < call->args.size(); ++i) {
      ExprResult arg = genExprResult(call->args[i].get());

      bool paramIsArray = false;
      if (sigIt != functions_.end() && i < sigIt->second.paramTypes.size()) {
        paramIsArray = sigIt->second.paramTypes[i].isArray;
      }

      if (arg.isArrayAccess && paramIsArray) {
        args.emplace_back(arg.addrOperand);
        argTypes.push_back(ValueType::I32);
      } else {
        if (sigIt != functions_.end() && i < sigIt->second.paramTypes.size()) {
          arg = castExprResult(arg, sigIt->second.paramTypes[i].withoutConst());
        }
        args.emplace_back(arg.operand);
        argTypes.push_back(toIRValueType(arg.type));
      }
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
    ExprResult lvalResult = genLValueExpr(assign->lvalue.get());
    if (!lvalResult.type.isValid()) return;

    ExprResult val = castExprResult(genExprResult(assign->value.get()),
                                    lvalResult.type.withoutConst());

    if (lvalResult.isArrayAccess) {
      current_->append<StoreInst>(toIRValueType(lvalResult.type), val.operand,
                                  lvalResult.addrOperand);
    } else if (lvalResult.operand.isGlobal()) {
      current_->append<StoreInst>(toIRValueType(lvalResult.type), val.operand,
                                  lvalResult.operand);
    } else {
      int vreg = lvalResult.operand.vregId;
      current_->append<CopyInst>(toIRValueType(lvalResult.type), vreg, val.operand);
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

bool IRGenerator::isZeroInitializedArray(VarDef* def) const {
  if (!def || !def->hasInit) {
    return true;
  }
  if (!def->hasInitList || !def->initList) {
    return false;
  }
  std::function<bool(const InitList*)> isZeroList = [&](const InitList* list) -> bool {
    if (!list) return true;
    if (list->elements.empty()) return true;
    if (list->isScalar) {
      if (list->elements.empty() || !list->elements[0]) return true;
      auto* expr = dynamic_cast<Expr*>(list->elements[0].get());
      auto* num = expr ? dynamic_cast<NumberExpr*>(expr) : nullptr;
      if (!num) return false;
      return num->scalarValue.isFloat() ? num->scalarValue.floatValue == 0.0f
                                        : num->scalarValue.intValue == 0;
    }
    for (const auto& elem : list->elements) {
      if (!elem) continue;
      if (auto* sub = dynamic_cast<InitList*>(elem.get())) {
        if (!isZeroList(sub)) return false;
        continue;
      }
      auto* expr = dynamic_cast<Expr*>(elem.get());
      auto* num = expr ? dynamic_cast<NumberExpr*>(expr) : nullptr;
      if (!num) return false;
      bool isZero = num->scalarValue.isFloat() ? num->scalarValue.floatValue == 0.0f
                                               : num->scalarValue.intValue == 0;
      if (!isZero) return false;
    }
    return true;
  };
  return isZeroList(def->initList.get());
}

int IRGenerator::calcArraySize(const std::vector<int>& dimensions) const {

  int size = 1;
  for (int dim : dimensions) {
    size *= dim;
  }
  return size;
}

void IRGenerator::flattenInitList(InitList* initList,
                                   std::vector<ScalarValue>& result,
                                   int totalSize) {
  if (!initList) return;

  if (initList->isScalar) {
    // 单个表达式
    if (!initList->elements.empty() && initList->elements[0]) {
      if (auto* expr = dynamic_cast<Expr*>(initList->elements[0].get())) {
        result.push_back(evalConstExpr(expr));
      }
    }
    return;
  }

  // 列表形式 - 递归展开
  for (auto& elem : initList->elements) {
    if (!elem) continue;
    if (result.size() >= static_cast<size_t>(totalSize)) break;

    if (auto* subList = dynamic_cast<InitList*>(elem.get())) {
      flattenInitList(subList, result, totalSize);
    } else if (auto* expr = dynamic_cast<Expr*>(elem.get())) {
      result.push_back(evalConstExpr(expr));
    }
  }
}

ScalarValue IRGenerator::evalConstExpr(Expr* expr) {
  if (!expr) return ScalarValue::Int(0);
  if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
    return num->scalarValue;
  }
  if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
    ScalarValue operand = evalConstExpr(unary->operand.get());
    switch (unary->op) {
      case UnaryExpr::U_PLUS:
        return operand;
      case UnaryExpr::U_MINUS:
        if (operand.isFloat()) {
          return ScalarValue::Float(-operand.floatValue);
        }
        return ScalarValue::Int(-operand.intValue);
      case UnaryExpr::U_NOT:
        if (operand.isFloat()) {
          return ScalarValue::Int(operand.floatValue == 0.0f ? 1 : 0);
        }
        return ScalarValue::Int(operand.intValue == 0 ? 1 : 0);
    }
  }
  // 对于当前不支持的非常量表达式，返回默认值
  return ScalarValue::Int(0);
}

void IRGenerator::collectInitElements(InitList* initList,
                                       std::vector<InitElement>& result,
                                       const std::vector<int>& dimensions) {
  if (!initList || dimensions.empty()) return;

  int totalSize = calcArraySize(dimensions);

  if (initList->isScalar) {
    // 单个表达式
    InitElement elem;
    if (!initList->elements.empty() && initList->elements[0]) {
      if (auto* expr = dynamic_cast<Expr*>(initList->elements[0].get())) {
        if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
          elem.isConst = true;
          elem.constValue = num->scalarValue;
        } else {
          elem.isConst = false;
          elem.expr = expr;
        }
      }
    }
    result.push_back(elem);
    return;
  }

  // 列表形式
  int subArraySize = 1;
  if (dimensions.size() > 1) {
    for (size_t i = 1; i < dimensions.size(); ++i) {
      subArraySize *= dimensions[i];
    }
  } else {
    subArraySize = 1;
  }

  size_t startSize = result.size();
  size_t maxEndSize = startSize + totalSize;

  for (size_t elemIdx = 0; elemIdx < initList->elements.size(); ++elemIdx) {
    auto& elem = initList->elements[elemIdx];
    if (result.size() >= maxEndSize) break;

    if (!elem) {
      result.push_back(InitElement{true, ScalarValue::Int(0), nullptr});
    } else if (auto* subList = dynamic_cast<InitList*>(elem.get())) {
      if (subList->isScalar) {
        // 标量子列表
        InitElement initElem;
        if (!subList->elements.empty() && subList->elements[0]) {
          if (auto* expr = dynamic_cast<Expr*>(subList->elements[0].get())) {
            if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
              initElem.isConst = true;
              initElem.constValue = num->scalarValue;
            } else {
              initElem.isConst = false;
              initElem.expr = expr;
            }
          }
        }
        result.push_back(initElem);
      } else {
        // 非标量子列表
        if (dimensions.size() > 1) {
          std::vector<int> subDims(dimensions.begin() + 1, dimensions.end());
          size_t prevSize = result.size();
          collectInitElements(subList, result, subDims);
          // 补 0
          int filled = static_cast<int>(result.size() - prevSize);
          for (int j = filled; j < subArraySize && result.size() < maxEndSize; ++j) {
            result.push_back(InitElement{true, ScalarValue::Int(0), nullptr});
          }
        } else {
          collectInitElements(subList, result, {1});
        }
      }
    } else if (auto* expr = dynamic_cast<Expr*>(elem.get())) {
      InitElement initElem;
      if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
        initElem.isConst = true;
        initElem.constValue = num->scalarValue;
      } else {
        initElem.isConst = false;
        initElem.expr = expr;
      }
      result.push_back(initElem);
    }
  }
}

void IRGenerator::flattenInitListWithDims(InitList* initList,
                                          std::vector<ScalarValue>& result,
                                          const std::vector<int>& dimensions) {
  if (!initList || dimensions.empty()) return;

  int totalSize = calcArraySize(dimensions);

  if (initList->isScalar) {
    // 单个表达式（包装在InitList中），只填充一个值
    if (!initList->elements.empty() && initList->elements[0]) {
      if (auto* expr = dynamic_cast<Expr*>(initList->elements[0].get())) {
        result.push_back(evalConstExpr(expr));
      } else {
        result.push_back(ScalarValue::Int(0));
      }
    } else {
      result.push_back(ScalarValue::Int(0));
    }
    return;
  }

  // 列表形式 - 按行优先顺序处理
  // 计算子数组大小（如果有多维）
  int subArraySize = 1;
  if (dimensions.size() > 1) {
    for (size_t i = 1; i < dimensions.size(); ++i) {
      subArraySize *= dimensions[i];
    }
  } else {
    // 一维数组，子数组大小就是维度本身（但实际不会有子数组）
    subArraySize = 1;
  }

  // 记录当前层级的起始位置，用于限制填充
  size_t startSize = result.size();
  size_t maxEndSize = startSize + totalSize;

  for (size_t elemIdx = 0; elemIdx < initList->elements.size(); ++elemIdx) {
    auto& elem = initList->elements[elemIdx];
    if (result.size() >= maxEndSize) break;

    if (!elem) {
      result.push_back(ScalarValue::Int(0));
    } else if (auto* subList = dynamic_cast<InitList*>(elem.get())) {
      if (subList->isScalar) {
        // 标量子列表：只填充一个值
        if (!subList->elements.empty() && subList->elements[0]) {
          if (auto* expr = dynamic_cast<Expr*>(subList->elements[0].get())) {
            result.push_back(evalConstExpr(expr));
          } else {
            result.push_back(ScalarValue::Int(0));
          }
        } else {
          result.push_back(ScalarValue::Int(0));
        }
      } else {
        // 非标量子列表（真正的嵌套列表）：对应一个完整的子数组
        if (dimensions.size() > 1) {
          std::vector<int> subDims(dimensions.begin() + 1, dimensions.end());
          size_t prevSize = result.size();
          flattenInitListWithDims(subList, result, subDims);
          // 如果子列表填充不足，补 0
          int filled = static_cast<int>(result.size() - prevSize);
          for (int j = filled; j < subArraySize && result.size() < maxEndSize; ++j) {
            result.push_back(ScalarValue::Int(0));
          }
        } else {
          // 一维数组遇到非标量子列表，递归展开
          flattenInitListWithDims(subList, result, {1});
        }
      }
    } else if (auto* expr = dynamic_cast<Expr*>(elem.get())) {
      // 标量表达式：直接添加
      result.push_back(evalConstExpr(expr));
    }
  }
}

IRGenerator::ExprResult IRGenerator::genArrayAccess(ArrayAccessExpr* node) {
  // 获取左值表达式（计算地址）
  ExprResult lvalResult = genLValueExpr(node);

  if (!lvalResult.type.isValid()) {
    return ExprResult{Type::Int(), Operand::Imm(0)};
  }

  // 如果结果是标量（不是数组），需要加载值
  if (!lvalResult.type.isArray) {
    ExprResult result;
    result.type = lvalResult.type;
    result.isArrayAccess = true;
    result.addrOperand = lvalResult.addrOperand;

    // 从地址加载值
    int dest = newVReg();
    current_->append<LoadInst>(toIRValueType(result.type), dest,
                               lvalResult.addrOperand);
    result.operand = Operand::VReg(dest, toIRValueType(result.type));
    return result;
  }

  // 如果还是数组，返回地址
  return lvalResult;
}

IRGenerator::ExprResult IRGenerator::genLValueExpr(Expr* expr) {
  if (auto* id = dynamic_cast<IdentifierExpr*>(expr)) {
    ValueBinding* binding = lookupBinding(id->name);
    if (!binding) {
      return ExprResult{Type::Invalid(), Operand::Imm(0)};
    }

    ExprResult result;
    result.type = binding->type.withoutConst();

    if (binding->isArray) {
      // 数组：返回基地址
      result.isArrayAccess = true;
      if (binding->isGlobal) {
        // 全局数组
        result.addrOperand = Operand::Global(binding->globalName, ValueType::I32);
      } else if (binding->isArrayParam) {
        // 数组参数：vreg 存储传入的数组地址
        result.addrOperand = Operand::VReg(binding->vreg, ValueType::I32);
      } else {
        // 局部数组：地址 = StackPtr + localVarOffset + offset
        result.addrOperand = Operand::LocalVarAddr(binding->stackOffset);
      }
      result.operand = result.addrOperand;
    } else {
      // 标量变量
      if (binding->isGlobal) {
        result.operand = Operand::Global(binding->globalName, toIRValueType(binding->type));
      } else {
        result.operand = Operand::VReg(binding->vreg, toIRValueType(binding->type));
      }
    }
    return result;
  }

  if (auto* arr = dynamic_cast<ArrayAccessExpr*>(expr)) {
    // 递归处理数组访问
    ExprResult baseResult = genLValueExpr(arr->array.get());

    if (!baseResult.type.isValid()) {
      return ExprResult{Type::Invalid(), Operand::Imm(0)};
    }

    if (!baseResult.type.isArray) {
      // 错误：对非数组进行下标访问
      return ExprResult{Type::Invalid(), Operand::Imm(0)};
    }

    // 计算索引表达式
    ExprResult indexResult = genExprResult(arr->index.get());

    // 计算偏移量
    // offset = index * (元素大小 * 后续维度的乘积)
    int elementCount = 1;
    if (baseResult.type.firstDimUnsized) {
      // firstDimUnsized: all explicit dims are "after" the consumed unsized dim
      for (int dim : baseResult.type.arrayDimensions) {
        elementCount *= dim;
      }
    } else if (baseResult.type.arrayDimensions.size() > 1) {
      for (size_t i = 1; i < baseResult.type.arrayDimensions.size(); ++i) {
        elementCount *= baseResult.type.arrayDimensions[i];
      }
    }

    // 计算地址: addr = base + index * (elementCount * 4)
    int strideBytes = elementCount * 4;
    Operand offsetOperand = Operand::Imm(0);
    if (indexResult.operand.isImm()) {
      offsetOperand = Operand::Imm(indexResult.operand.immValue * strideBytes);
    } else {
      int offsetReg = newVReg();
      current_->append<BinaryInst>(BinaryOp::Mul, ValueType::I32, ValueType::I32,
                                   offsetReg, indexResult.operand,
                                   Operand::Imm(strideBytes));
      offsetOperand = Operand::VReg(offsetReg, ValueType::I32);
    }

    // 计算最终地址
    Operand finalAddr = baseResult.addrOperand;
    if (offsetOperand.isImm()) {
      if (offsetOperand.immValue != 0) {
        if (baseResult.addrOperand.isLocalVarAddr()) {
          finalAddr = Operand::LocalVarAddr(baseResult.addrOperand.immValue +
                                            offsetOperand.immValue);
        } else {
          int addrReg = newVReg();
          current_->append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32,
                                       addrReg, baseResult.addrOperand, offsetOperand);
          finalAddr = Operand::VReg(addrReg, ValueType::I32);
        }
      }
    } else {
      int addrReg = newVReg();
      current_->append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32,
                                   addrReg, baseResult.addrOperand, offsetOperand);
      finalAddr = Operand::VReg(addrReg, ValueType::I32);
    }

    // 构造结果类型（减少一个维度）
    ExprResult result;
    result.isArrayAccess = true;
    result.addrOperand = finalAddr;

    Type resultType;
    resultType.base = baseResult.type.base;
    resultType.isConst = baseResult.type.isConst;

    {
      int totalDims = static_cast<int>(baseResult.type.arrayDimensions.size()) + (baseResult.type.firstDimUnsized ? 1 : 0);
      int remaining = totalDims - 1;
      if (remaining > 0) {
        resultType.isArray = true;
        if (baseResult.type.firstDimUnsized) {
          resultType.arrayDimensions = baseResult.type.arrayDimensions;
        } else {
          resultType.arrayDimensions = std::vector<int>(
              baseResult.type.arrayDimensions.begin() + 1,
              baseResult.type.arrayDimensions.end());
        }
        resultType.firstDimUnsized = false;
      } else {
        resultType.isArray = false;
      }
    }

    result.type = resultType;
    result.operand = result.addrOperand;
    return result;
  }

  return ExprResult{Type::Invalid(), Operand::Imm(0)};
}

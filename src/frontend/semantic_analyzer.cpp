#include "semantic_analyzer.h"

#include <sstream>

void SemanticAnalyzer::declareBuiltinFunctions() {
  symbolTable.declareFunction("getint", Type::Int(), {});
  symbolTable.declareFunction("getch", Type::Int(), {});
  symbolTable.declareFunction("getfloat", Type::Float(), {});

  symbolTable.declareFunction("putint", Type::Void(), {Type::Int()});
  symbolTable.declareFunction("putch", Type::Void(), {Type::Int()});
  symbolTable.declareFunction("putfloat", Type::Void(), {Type::Float()});

  // 数组相关内置函数
  Type intArrayParam = Type::Int();
  intArrayParam.isArray = true;
  intArrayParam.firstDimUnsized = true;

  Type floatArrayParam = Type::Float();
  floatArrayParam.isArray = true;
  floatArrayParam.firstDimUnsized = true;

  symbolTable.declareFunction("getarray", Type::Int(), {intArrayParam});
  symbolTable.declareFunction("getfarray", Type::Int(), {floatArrayParam});
  symbolTable.declareFunction("putarray", Type::Void(), {Type::Int(), intArrayParam});
  symbolTable.declareFunction("putfarray", Type::Void(), {Type::Int(), floatArrayParam});
}

bool SemanticAnalyzer::isIntType(const Type& type) const {
  return type.base == BaseType::INT && !type.isArray;
}

bool SemanticAnalyzer::isFloatType(const Type& type) const {
  return type.base == BaseType::FLOAT && !type.isArray;
}

bool SemanticAnalyzer::isNumericType(const Type& type) const {
  return (isIntType(type) || isFloatType(type));
}

// 检查是否为数值类型或数值数组
bool SemanticAnalyzer::isNumericOrArrayBase(const Type& type) const {
  return type.base == BaseType::INT || type.base == BaseType::FLOAT;
}

bool SemanticAnalyzer::canImplicitlyConvert(const Type& from,
                                            const Type& to) const {
  // 数组类型：需要类型匹配（int[] 匹配 int[]，float[] 匹配 float[]）
  // 维度不需要完全匹配（因为数组参数会退化为指针）
  if (from.isArray && to.isArray) {
    return from.base == to.base;
  }
  // 标量类型：可以隐式转换
  if (!isNumericType(from) || !isNumericType(to)) return false;
  return true;
}

// 检查参数类型是否兼容（用于函数调用）
bool SemanticAnalyzer::isParamCompatible(const Type& argType, const Type& paramType) const {
  // 如果参数是数组类型
  if (paramType.isArray) {
    // 实参也必须是数组类型，且基础类型相同
    if (!argType.isArray) return false;
    return argType.base == paramType.base;
  }
  // 参数是标量类型
  if (argType.isArray) return false;  // 不能把数组传给标量参数
  return isNumericType(argType) && isNumericType(paramType);
}

ScalarValue SemanticAnalyzer::castConstValue(const ScalarValue& value,
                                             const Type& targetType) const {
  if (isFloatType(targetType)) {
    return value.isFloat() ? value : ScalarValue::Float(static_cast<float>(value.intValue));
  }
  if (isIntType(targetType)) {
    return value.isFloat() ? ScalarValue::Int(static_cast<int>(value.floatValue))
                           : value;
  }
  return ScalarValue::Int(0);
}

bool SemanticAnalyzer::isTruthy(const ScalarValue& value) const {
  if (value.isFloat()) return value.floatValue != 0.0f;
  return value.intValue != 0;
}

Type SemanticAnalyzer::commonNumericType(const Type& lhs, const Type& rhs) const {
  if (isFloatType(lhs) || isFloatType(rhs)) return Type::Float();
  return Type::Int();
}

void SemanticAnalyzer::setExprResult(const Type& type, bool isConst,
                                     ScalarValue constValue) {
  lastExprType = type;
  lastExprIsConst = isConst;
  lastExprConstValue = constValue;
}

bool SemanticAnalyzer::evalConstExpr(Expr* expr, ScalarValue* value) {
  if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
    *value = num->scalarValue;
    return true;
  }
  if (auto* id = dynamic_cast<IdentifierExpr*>(expr)) {
    Symbol* sym = symbolTable.lookupValue(id->name);
    if (!sym || !sym->hasConstValue) return false;
    *value = sym->typedConstValue;
    return true;
  }
  if (auto* paren = dynamic_cast<ParenExpr*>(expr)) {
    return evalConstExpr(paren->expr.get(), value);
  }
  if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
    ScalarValue operand;
    if (!evalConstExpr(unary->operand.get(), &operand)) return false;
    switch (unary->op) {
      case UnaryExpr::U_PLUS:
        *value = operand;
        return true;
      case UnaryExpr::U_MINUS:
        if (operand.isFloat()) *value = ScalarValue::Float(-operand.floatValue);
        else *value = ScalarValue::Int(-operand.intValue);
        return true;
      case UnaryExpr::U_NOT:
        *value = ScalarValue::Int(isTruthy(operand) ? 0 : 1);
        return true;
    }
  }
  if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
    ScalarValue lhs;
    ScalarValue rhs;
    if (!evalConstExpr(binary->left.get(), &lhs) ||
        !evalConstExpr(binary->right.get(), &rhs)) {
      return false;
    }

    bool useFloat = lhs.isFloat() || rhs.isFloat();
    float lf = lhs.isFloat() ? lhs.floatValue : static_cast<float>(lhs.intValue);
    float rf = rhs.isFloat() ? rhs.floatValue : static_cast<float>(rhs.intValue);
    int li = lhs.isFloat() ? static_cast<int>(lhs.floatValue) : lhs.intValue;
    int ri = rhs.isFloat() ? static_cast<int>(rhs.floatValue) : rhs.intValue;

    switch (binary->op) {
      case BinaryExpr::B_ADD:
        *value = useFloat ? ScalarValue::Float(lf + rf)
                          : ScalarValue::Int(li + ri);
        return true;
      case BinaryExpr::B_SUB:
        *value = useFloat ? ScalarValue::Float(lf - rf)
                          : ScalarValue::Int(li - ri);
        return true;
      case BinaryExpr::B_MUL:
        *value = useFloat ? ScalarValue::Float(lf * rf)
                          : ScalarValue::Int(li * ri);
        return true;
      case BinaryExpr::B_DIV:
        if ((useFloat && rf == 0.0f) || (!useFloat && ri == 0)) return false;
        *value = useFloat ? ScalarValue::Float(lf / rf)
                          : ScalarValue::Int(li / ri);
        return true;
      case BinaryExpr::B_MOD:
        if (useFloat || ri == 0) return false;
        *value = ScalarValue::Int(li % ri);
        return true;
      case BinaryExpr::B_LT:
        *value = ScalarValue::Int(useFloat ? (lf < rf) : (li < ri));
        return true;
      case BinaryExpr::B_GT:
        *value = ScalarValue::Int(useFloat ? (lf > rf) : (li > ri));
        return true;
      case BinaryExpr::B_LE:
        *value = ScalarValue::Int(useFloat ? (lf <= rf) : (li <= ri));
        return true;
      case BinaryExpr::B_GE:
        *value = ScalarValue::Int(useFloat ? (lf >= rf) : (li >= ri));
        return true;
      case BinaryExpr::B_EQ:
        *value = ScalarValue::Int(useFloat ? (lf == rf) : (li == ri));
        return true;
      case BinaryExpr::B_NE:
        *value = ScalarValue::Int(useFloat ? (lf != rf) : (li != ri));
        return true;
      case BinaryExpr::B_AND:
        *value = ScalarValue::Int(isTruthy(lhs) && isTruthy(rhs));
        return true;
      case BinaryExpr::B_OR:
        *value = ScalarValue::Int(isTruthy(lhs) || isTruthy(rhs));
        return true;
    }
  }
  return false;
}

void SemanticAnalyzer::visit(NumberExpr* node) {
  if (node->isFloatLiteral()) setExprResult(Type::Float(), true, node->scalarValue);
  else setExprResult(Type::Int(), true, node->scalarValue);
}

void SemanticAnalyzer::visit(IdentifierExpr* node) {
  Symbol* sym = symbolTable.lookupValue(node->name);
  if (!sym) {
    addError("Identifier '" + node->name + "' used before declaration");
    setExprResult(Type::Invalid());
    return;
  }

  // 如果是数组类型，保持数组信息
  Type resultType = sym->type.withoutConst();
  setExprResult(resultType, sym->hasConstValue, sym->typedConstValue);
}

void SemanticAnalyzer::visit(ParenExpr* node) { node->expr->accept(this); }

void SemanticAnalyzer::visit(FunctionCallExpr* node) {
  Symbol* func = symbolTable.lookupFunction(node->funcName);
  if (!func) {
    addError("Function '" + node->funcName + "' not declared");
    setExprResult(Type::Invalid());
    return;
  }

  if (func->paramTypes.size() != node->args.size()) {
    std::stringstream ss;
    ss << "Function '" << node->funcName << "' expects "
       << func->paramTypes.size() << " arguments, but " << node->args.size()
       << " provided";
    addError(ss.str());
  }

  for (size_t i = 0; i < node->args.size(); ++i) {
    node->args[i]->accept(this);
    // 检查参数类型是否兼容
    if (i < func->paramTypes.size()) {
      if (!isParamCompatible(lastExprType, func->paramTypes[i])) {
        if (lastExprType.isArray && !func->paramTypes[i].isArray) {
          addError("Cannot pass array to scalar parameter");
        } else if (!lastExprType.isArray && func->paramTypes[i].isArray) {
          addError("Cannot pass scalar to array parameter");
        } else if (lastExprType.isArray && func->paramTypes[i].isArray &&
                   lastExprType.base != func->paramTypes[i].base) {
          addError("Array element type mismatch in function call");
        } else if (!isNumericType(lastExprType) && !lastExprType.isArray) {
          addError("Function argument must be int, float, or array");
        }
      }
    }
  }

  setExprResult(func->type.withoutConst(), false, ScalarValue::Int(0));
}

void SemanticAnalyzer::visit(UnaryExpr* node) {
  node->operand->accept(this);
  if (!isNumericType(lastExprType)) {
    addError("Unary operator requires int or float operand");
    setExprResult(Type::Invalid());
    return;
  }

  ScalarValue constValue = ScalarValue::Int(0);
  bool isConst = false;
  Type resultType = node->op == UnaryExpr::U_NOT ? Type::Int() : lastExprType.withoutConst();
  if (lastExprIsConst) {
    isConst = true;
    switch (node->op) {
      case UnaryExpr::U_PLUS:
        constValue = lastExprConstValue;
        break;
      case UnaryExpr::U_MINUS:
        if (lastExprConstValue.isFloat()) {
          constValue = ScalarValue::Float(-lastExprConstValue.floatValue);
        } else {
          constValue = ScalarValue::Int(-lastExprConstValue.intValue);
        }
        break;
      case UnaryExpr::U_NOT:
        constValue = ScalarValue::Int(isTruthy(lastExprConstValue) ? 0 : 1);
        break;
    }
  }
  setExprResult(resultType, isConst, constValue);
}

void SemanticAnalyzer::visit(BinaryExpr* node) {
  node->left->accept(this);
  Type lhsType = lastExprType;
  bool lhsConst = lastExprIsConst;
  ScalarValue lhsValue = lastExprConstValue;
  if (!isNumericType(lhsType)) {
    addError("Binary operator requires int or float operands");
  }

  node->right->accept(this);
  Type rhsType = lastExprType;
  bool rhsConst = lastExprIsConst;
  ScalarValue rhsValue = lastExprConstValue;
  if (!isNumericType(rhsType)) {
    addError("Binary operator requires int or float operands");
  }

  if (node->op == BinaryExpr::B_MOD && (!isIntType(lhsType) || !isIntType(rhsType))) {
    addError("Modulo operator requires int operands");
  }

  Type resultType = Type::Int();
  switch (node->op) {
    case BinaryExpr::B_ADD:
    case BinaryExpr::B_SUB:
    case BinaryExpr::B_MUL:
    case BinaryExpr::B_DIV:
      resultType = commonNumericType(lhsType, rhsType);
      break;
    case BinaryExpr::B_MOD:
      resultType = Type::Int();
      break;
    default:
      resultType = Type::Int();
      break;
  }

  bool isConst = lhsConst && rhsConst;
  ScalarValue constValue = ScalarValue::Int(0);
  if (isConst) {
    float lf = lhsValue.isFloat() ? lhsValue.floatValue : static_cast<float>(lhsValue.intValue);
    float rf = rhsValue.isFloat() ? rhsValue.floatValue : static_cast<float>(rhsValue.intValue);
    int li = lhsValue.isFloat() ? static_cast<int>(lhsValue.floatValue) : lhsValue.intValue;
    int ri = rhsValue.isFloat() ? static_cast<int>(rhsValue.floatValue) : rhsValue.intValue;

    switch (node->op) {
      case BinaryExpr::B_ADD:
        constValue = resultType.isFloatScalar() ? ScalarValue::Float(lf + rf)
                                                : ScalarValue::Int(li + ri);
        break;
      case BinaryExpr::B_SUB:
        constValue = resultType.isFloatScalar() ? ScalarValue::Float(lf - rf)
                                                : ScalarValue::Int(li - ri);
        break;
      case BinaryExpr::B_MUL:
        constValue = resultType.isFloatScalar() ? ScalarValue::Float(lf * rf)
                                                : ScalarValue::Int(li * ri);
        break;
      case BinaryExpr::B_DIV:
        if ((resultType.isFloatScalar() && rf == 0.0f) ||
            (!resultType.isFloatScalar() && ri == 0)) {
          isConst = false;
        } else {
          constValue = resultType.isFloatScalar() ? ScalarValue::Float(lf / rf)
                                                  : ScalarValue::Int(li / ri);
        }
        break;
      case BinaryExpr::B_MOD:
        if (ri == 0) isConst = false;
        else constValue = ScalarValue::Int(li % ri);
        break;
      case BinaryExpr::B_LT:
        constValue = ScalarValue::Int((resultType.isFloatScalar() || lhsValue.isFloat() || rhsValue.isFloat()) ? (lf < rf) : (li < ri));
        break;
      case BinaryExpr::B_GT:
        constValue = ScalarValue::Int((resultType.isFloatScalar() || lhsValue.isFloat() || rhsValue.isFloat()) ? (lf > rf) : (li > ri));
        break;
      case BinaryExpr::B_LE:
        constValue = ScalarValue::Int((resultType.isFloatScalar() || lhsValue.isFloat() || rhsValue.isFloat()) ? (lf <= rf) : (li <= ri));
        break;
      case BinaryExpr::B_GE:
        constValue = ScalarValue::Int((resultType.isFloatScalar() || lhsValue.isFloat() || rhsValue.isFloat()) ? (lf >= rf) : (li >= ri));
        break;
      case BinaryExpr::B_EQ:
        constValue = ScalarValue::Int((lhsValue.isFloat() || rhsValue.isFloat()) ? (lf == rf) : (li == ri));
        break;
      case BinaryExpr::B_NE:
        constValue = ScalarValue::Int((lhsValue.isFloat() || rhsValue.isFloat()) ? (lf != rf) : (li != ri));
        break;
      case BinaryExpr::B_AND:
        constValue = ScalarValue::Int(isTruthy(lhsValue) && isTruthy(rhsValue));
        break;
      case BinaryExpr::B_OR:
        constValue = ScalarValue::Int(isTruthy(lhsValue) || isTruthy(rhsValue));
        break;
    }
  }

  setExprResult(resultType, isConst, constValue);
}

void SemanticAnalyzer::visit(Block* node) {
  symbolTable.enterScope();
  bool blockReturns = false;
  bool blockBreaks = false;
  bool blockFallsThrough = true;
  for (auto& stmt : node->stmts) {
    if (!blockFallsThrough) {
      break;
    }
    hasReturn = false;
    hasBreak = false;
    hasFallthrough = true;
    stmt->accept(this);
    if (hasReturn) {
      blockReturns = true;
    }
    if (hasBreak) {
      blockBreaks = true;
    }
    blockFallsThrough = hasFallthrough;
  }
  hasReturn = blockReturns;
  hasBreak = blockBreaks;
  hasFallthrough = blockFallsThrough;
  symbolTable.exitScope();
}

void SemanticAnalyzer::visit(EmptyStmt* node) {
  (void)node;
  hasReturn = false;
  hasBreak = false;
  hasFallthrough = true;
}

void SemanticAnalyzer::visit(ExprStmt* node) {
  node->expr->accept(this);
  hasReturn = false;
  hasBreak = false;
  hasFallthrough = true;
}

void SemanticAnalyzer::visit(AssignStmt* node) {
  LValueInfo lvalInfo = analyzeLValue(node->lvalue.get());

  if (!lvalInfo.isValid) {
    // 错误已在 analyzeLValue 中报告
    node->value->accept(this);
    hasReturn = false;
    hasBreak = false;
    hasFallthrough = true;
    return;
  }

  if (lvalInfo.isConst) {
    addError("Cannot assign to const variable '" + lvalInfo.name + "'");
  }

  node->value->accept(this);

  // 检查赋值类型兼容性
  Type targetType = lvalInfo.type;
  if (!isNumericType(lastExprType)) {
    addError("Assigned value must be int or float expression");
  } else if (!canImplicitlyConvert(lastExprType, targetType)) {
    addError("Assigned value type mismatch for '" + lvalInfo.name + "'");
  }

  hasReturn = false;
  hasBreak = false;
  hasFallthrough = true;
}

void SemanticAnalyzer::visitDeclDefs(VarDeclStmt* node) {
  for (auto& defPtr : node->defs) {
    VarDef* def = defPtr.get();
    bool initIsConst = false;
    ScalarValue constValue = ScalarValue::Int(0);
    std::vector<ScalarValue> constArrayValues;

    // 处理数组维度
    Type varType = node->declaredType;
    if (def->isArray) {
      if (!validateArrayDimensions(def)) {
        continue;
      }
      varType.isArray = true;
      varType.arrayDimensions.clear();
      for (auto& dimExpr : def->arrayDimExprs) {
        ScalarValue dimValue;
        if (evalConstExpr(dimExpr.get(), &dimValue)) {
          int dim = dimValue.isFloat() ? static_cast<int>(dimValue.floatValue) : dimValue.intValue;
          varType.arrayDimensions.push_back(dim);
        }
      }
    }

    // 处理初始化表达式
    if (def->initExpr) {
      def->initExpr->accept(this);
      if (!def->isArray) {
        if (!isNumericType(lastExprType)) {
          addError("Initializer for '" + def->name + "' must be numeric expression");
        } else if (!canImplicitlyConvert(lastExprType, varType)) {
          addError("Initializer type mismatch for '" + def->name + "'");
        }
        initIsConst = evalConstExpr(def->initExpr.get(), &constValue);
        if (initIsConst) {
          constValue = castConstValue(constValue, varType);
        }
      } else {
        // 数组不能用单一表达式初始化
        addError("Array '" + def->name + "' must be initialized with braces");
      }
    } else if (def->initList) {
      if (!def->isArray) {
        addError("Scalar variable '" + def->name + "' cannot be initialized with braces");
      } else {
        if (validateInitList(def->initList.get(), varType, &constArrayValues)) {
          initIsConst = true;
        }
      }
    }

    if (!symbolTable.declareValue(def->name, varType, false,
                                  varType.isConst && initIsConst,
                                  constValue)) {
      addError("Identifier '" + def->name + "' already declared in this scope");
      continue;
    }

    def->initIsConst = initIsConst;
    if (!def->hasInitList) {
      def->typedConstInitValue = constValue;
      if (constValue.isInt()) def->constInitValue = constValue.intValue;
      else def->constInitValue = static_cast<int>(constValue.floatValue);
    }
  }
}

void SemanticAnalyzer::visit(VarDeclStmt* node) {
  visitDeclDefs(node);
  hasReturn = false;
  hasBreak = false;
  hasFallthrough = true;
}

void SemanticAnalyzer::visit(IfStmt* node) {
  node->condition->accept(this);
  if (!isNumericType(lastExprType)) {
    addError("If condition must be int or float expression");
  }

  bool thenHasReturn = false;
  bool thenHasBreak = false;
  bool thenFallsThrough = true;
  hasReturn = false;
  hasBreak = false;
  hasFallthrough = true;
  node->thenStmt->accept(this);
  thenHasReturn = hasReturn;
  thenHasBreak = hasBreak;
  thenFallsThrough = hasFallthrough;

  bool elseHasReturn = false;
  bool elseHasBreak = false;
  bool elseFallsThrough = true;
  if (node->elseStmt) {
    hasReturn = false;
    hasBreak = false;
    hasFallthrough = true;
    node->elseStmt->accept(this);
    elseHasReturn = hasReturn;
    elseHasBreak = hasBreak;
    elseFallsThrough = hasFallthrough;
  }

  hasReturn = thenHasReturn && node->elseStmt && elseHasReturn;
  hasBreak = thenHasBreak || elseHasBreak;
  hasFallthrough = node->elseStmt ? (thenFallsThrough || elseFallsThrough) : true;
}

void SemanticAnalyzer::visit(WhileStmt* node) {
  node->condition->accept(this);
  if (!isNumericType(lastExprType)) {
    addError("While condition must be int or float expression");
  }

  bool conditionAlwaysTrue = false;
  ScalarValue condValue;
  if (evalConstExpr(node->condition.get(), &condValue)) {
    conditionAlwaysTrue = isTruthy(condValue);
  }

  loopDepth++;
  hasReturn = false;
  hasBreak = false;
  hasFallthrough = true;
  node->body->accept(this);
  bool bodyHasReturn = hasReturn;
  bool bodyHasBreak = hasBreak;
  bool bodyFallsThrough = hasFallthrough;
  loopDepth--;

  hasReturn = conditionAlwaysTrue && bodyHasReturn && !bodyHasBreak;
  hasBreak = false;
  hasFallthrough = !conditionAlwaysTrue || bodyHasBreak;
}

void SemanticAnalyzer::visit(BreakStmt* node) {
  (void)node;
  if (loopDepth == 0) {
    addError("'break' statement not in loop");
  }
  hasReturn = false;
  hasBreak = true;
  hasFallthrough = false;
}

void SemanticAnalyzer::visit(ContinueStmt* node) {
  (void)node;
  if (loopDepth == 0) {
    addError("'continue' statement not in loop");
  }
  hasReturn = false;
  hasBreak = false;
  hasFallthrough = false;
}

void SemanticAnalyzer::visit(ReturnStmt* node) {
  if (!currentFunction) {
    addError("'return' statement outside of function");
    return;
  }

  if (!node->returnValue) {
    if (currentFunction->returnType.base != BaseType::VOID) {
      addError("Non-void function '" + currentFunction->name + "' must return a value");
    }
    hasReturn = true;
    hasBreak = false;
    hasFallthrough = false;
    return;
  }

  node->returnValue->accept(this);
  if (currentFunction->returnType.base == BaseType::VOID) {
    addError("Void function '" + currentFunction->name + "' cannot return a value");
  } else if (!isNumericType(lastExprType) ||
             !canImplicitlyConvert(lastExprType, currentFunction->returnType)) {
    addError("Non-void function '" + currentFunction->name + "' has incompatible return expression");
  }
  hasReturn = true;
  hasBreak = false;
  hasFallthrough = false;
}

void SemanticAnalyzer::visit(FuncDef* node) {
  currentFunction = node;
  hasReturn = false;
  hasBreak = false;
  hasFallthrough = true;
  symbolTable.enterScope();

  for (auto& param : node->params) {
    // Evaluate array dimension expressions for array parameters (if not already done)
    if (param->type.isArray && param->type.arrayDimensions.empty()) {
      for (auto& dimExpr : param->arrayDimExprs) {
        ScalarValue dimValue;
        if (evalConstExpr(dimExpr.get(), &dimValue)) {
          int dim = dimValue.isFloat() ? static_cast<int>(dimValue.floatValue) : dimValue.intValue;
          param->type.arrayDimensions.push_back(dim);
        }
      }
    }
    if (!symbolTable.declareValue(param->name, param->type, true, false,
                                  ScalarValue::Int(0))) {
      addError("Parameter '" + param->name + "' already declared");
    }
  }

  node->body->accept(this);

  if ((node->returnType.base == BaseType::INT ||
       node->returnType.base == BaseType::FLOAT) &&
      hasFallthrough) {
    addError("Non-void function '" + node->name + "' must return a value on all execution paths");
  }

  symbolTable.exitScope();
  currentFunction = nullptr;
  hasBreak = false;
  hasFallthrough = false;
}

void SemanticAnalyzer::visit(CompUnit* node) {

  for (auto& item : node->items) {
    if (auto* decl = dynamic_cast<VarDeclStmt*>(item.get())) {
      decl->accept(this);
      continue;
    }
    if (auto* func = dynamic_cast<FuncDef*>(item.get())) {
      // Evaluate array dimension expressions for parameters before collecting types
      for (auto& param : func->params) {
        if (param->type.isArray && param->type.arrayDimensions.empty()) {
          for (auto& dimExpr : param->arrayDimExprs) {
            ScalarValue dimValue;
            if (evalConstExpr(dimExpr.get(), &dimValue)) {
              int dim = dimValue.isFloat() ? static_cast<int>(dimValue.floatValue) : dimValue.intValue;
              param->type.arrayDimensions.push_back(dim);
            }
          }
        }
      }
      std::vector<Type> paramTypes;
      for (auto& param : func->params) {
        paramTypes.push_back(param->type);
      }
      if (!symbolTable.declareFunction(func->name, func->returnType, paramTypes)) {
        addError("Function '" + func->name + "' already declared");
        continue;
      }
      func->accept(this);
    }
  }
}

// ==================== 数组相关实现 ====================

void SemanticAnalyzer::visit(ArrayAccessExpr* node) {
  // 递归分析数组部分
  node->array->accept(this);
  Type arrayType = lastExprType;

  if (!arrayType.isArray) {
    addError("Array subscript requires array type");
    setExprResult(Type::Invalid());
    return;
  }

  // 检查索引类型
  node->index->accept(this);
  if (!isIntType(lastExprType)) {
    addError("Array index must be integer");
  }

  // 计算结果类型（减少一个维度）
  Type resultType;
  resultType.base = arrayType.base;
  resultType.isConst = arrayType.isConst;

  // Total dimensions = arrayDimensions.size() + (firstDimUnsized ? 1 : 0)
  // After indexing, remaining = total - 1
  int totalDims = static_cast<int>(arrayType.arrayDimensions.size()) + (arrayType.firstDimUnsized ? 1 : 0);
  int remaining = totalDims - 1;

  if (remaining > 0) {
    resultType.isArray = true;
    if (arrayType.firstDimUnsized) {
      // Consumed the unsized dim; explicit dims remain as-is
      resultType.arrayDimensions = arrayType.arrayDimensions;
    } else {
      // Drop the first explicit dimension
      resultType.arrayDimensions = std::vector<int>(
          arrayType.arrayDimensions.begin() + 1, arrayType.arrayDimensions.end());
    }
    resultType.firstDimUnsized = false;
  } else {
    resultType.isArray = false;
  }

  setExprResult(resultType, false, ScalarValue::Int(0));
}

void SemanticAnalyzer::visit(InitList* node) {
  // InitList 本身不设置表达式结果，由父节点处理
  // 这里只是遍历元素
  for (auto& elem : node->elements) {
    if (auto* expr = dynamic_cast<Expr*>(elem.get())) {
      expr->accept(this);
    } else if (auto* subList = dynamic_cast<InitList*>(elem.get())) {
      subList->accept(this);
    }
  }
}

LValueInfo SemanticAnalyzer::analyzeLValue(Expr* expr) {
  LValueInfo info;

  if (auto* id = dynamic_cast<IdentifierExpr*>(expr)) {
    info.name = id->name;
    info.symbol = symbolTable.lookupValue(id->name);

    if (!info.symbol) {
      addError("Variable '" + id->name + "' used before declaration");
      return info;
    }

    info.type = info.symbol->type;
    info.isValid = true;
    info.isConst = (info.symbol->kind == SymbolKind::CONSTANT);
    return info;
  }

  if (auto* arr = dynamic_cast<ArrayAccessExpr*>(expr)) {
    LValueInfo baseInfo = analyzeLValue(arr->array.get());

    if (!baseInfo.isValid) {
      return info;
    }

    if (!baseInfo.type.isArray) {
      addError("Array subscript requires array type");
      return info;
    }

    // 检查索引类型
    arr->index->accept(this);
    if (!isIntType(lastExprType)) {
      addError("Array index must be integer");
    }

    info.name = baseInfo.name;
    info.symbol = baseInfo.symbol;

    // 减少一个维度
    Type resultType;
    resultType.base = baseInfo.type.base;
    resultType.isConst = baseInfo.type.isConst;

    {
      int totalDims = static_cast<int>(baseInfo.type.arrayDimensions.size()) + (baseInfo.type.firstDimUnsized ? 1 : 0);
      int remaining = totalDims - 1;
      if (remaining > 0) {
        resultType.isArray = true;
        if (baseInfo.type.firstDimUnsized) {
          resultType.arrayDimensions = baseInfo.type.arrayDimensions;
        } else {
          resultType.arrayDimensions = std::vector<int>(
              baseInfo.type.arrayDimensions.begin() + 1,
              baseInfo.type.arrayDimensions.end());
        }
        resultType.firstDimUnsized = false;
      } else {
        resultType.isArray = false;
      }
    }

    info.type = resultType;
    info.isValid = true;
    info.isConst = baseInfo.isConst;
    info.indices = baseInfo.indices;
    info.indices.push_back(arr->index.get());

    return info;
  }

  addError("Invalid left-value expression");
  return info;
}

bool SemanticAnalyzer::validateArrayDimensions(VarDef* def) {
  if (!def->isArray) return true;

  def->arrayDimensions.clear();

  for (auto& dimExpr : def->arrayDimExprs) {
    ScalarValue dimValue;
    if (!evalConstExpr(dimExpr.get(), &dimValue)) {
      addError("Array dimension must be constant expression for '" + def->name + "'");
      return false;
    }

    int dim = dimValue.isFloat() ? static_cast<int>(dimValue.floatValue) : dimValue.intValue;
    if (dim < 0) {
      addError("Array dimension must be non-negative for '" + def->name + "'");
      return false;
    }

    def->arrayDimensions.push_back(dim);
  }

  return true;
}

bool SemanticAnalyzer::validateInitList(InitList* initList, const Type& arrayType,
                                        std::vector<ScalarValue>* flattenedValues) {
  if (!arrayType.isArray) {
    addError("Init list can only be used for array initialization");
    return false;
  }

  int totalSize = 1;
  for (int dim : arrayType.arrayDimensions) {
    totalSize *= dim;
  }

  flattenedValues->clear();
  flattenedValues->reserve(totalSize);

  int index = 0;
  flattenInitList(initList, arrayType, *flattenedValues, index, totalSize);

  // 填充剩余元素为 0
  Type elemType;
  elemType.base = arrayType.base;
  while (flattenedValues->size() < static_cast<size_t>(totalSize)) {
    if (elemType.base == BaseType::FLOAT) {
      flattenedValues->push_back(ScalarValue::Float(0.0f));
    } else {
      flattenedValues->push_back(ScalarValue::Int(0));
    }
  }

  return true;
}

void SemanticAnalyzer::flattenInitList(InitList* initList, const Type& elemType,
                                       std::vector<ScalarValue>& result, int& index, int totalSize) {
  if (index >= totalSize) return;

  if (initList->isScalar) {
    // 单个表达式
    if (!initList->elements.empty()) {
      if (auto* expr = dynamic_cast<Expr*>(initList->elements[0].get())) {
        expr->accept(this);
        ScalarValue value;
        if (evalConstExpr(expr, &value)) {
          // 类型转换
          if (elemType.base == BaseType::FLOAT && !value.isFloat()) {
            value = ScalarValue::Float(static_cast<float>(value.intValue));
          } else if (elemType.base == BaseType::INT && value.isFloat()) {
            value = ScalarValue::Int(static_cast<int>(value.floatValue));
          }
          result.push_back(value);
          index++;
        } else {
          // 非常量初始化，暂时填 0
          result.push_back(elemType.base == BaseType::FLOAT ?
                          ScalarValue::Float(0.0f) : ScalarValue::Int(0));
          index++;
        }
      }
    }
    return;
  }

  // 列表形式
  for (auto& elem : initList->elements) {
    if (index >= totalSize) break;

    if (auto* subList = dynamic_cast<InitList*>(elem.get())) {
      flattenInitList(subList, elemType, result, index, totalSize);
    } else if (auto* expr = dynamic_cast<Expr*>(elem.get())) {
      ScalarValue value;
      if (evalConstExpr(expr, &value)) {
        if (elemType.base == BaseType::FLOAT && !value.isFloat()) {
          value = ScalarValue::Float(static_cast<float>(value.intValue));
        } else if (elemType.base == BaseType::INT && value.isFloat()) {
          value = ScalarValue::Int(static_cast<int>(value.floatValue));
        }
        result.push_back(value);
        index++;
      } else {
        result.push_back(elemType.base == BaseType::FLOAT ?
                        ScalarValue::Float(0.0f) : ScalarValue::Int(0));
        index++;
      }
    }
  }
}

#include "semantic_analyzer.h"

#include <sstream>

void SemanticAnalyzer::declareBuiltinFunctions() {
  symbolTable.declareFunction("getint", Type::Int(), {});
  symbolTable.declareFunction("getch", Type::Int(), {});
  symbolTable.declareFunction("getfloat", Type::Float(), {});

  symbolTable.declareFunction("putint", Type::Void(), {Type::Int()});
  symbolTable.declareFunction("putch", Type::Void(), {Type::Int()});
  symbolTable.declareFunction("putfloat", Type::Void(), {Type::Float()});
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

bool SemanticAnalyzer::canImplicitlyConvert(const Type& from,
                                            const Type& to) const {
  if (!isNumericType(from) || !isNumericType(to)) return false;
  return true;
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
  setExprResult(sym->type.withoutConst(), sym->hasConstValue, sym->typedConstValue);
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
    if (!isNumericType(lastExprType)) {
      addError("Function argument must be int or float expression");
      continue;
    }
    if (i < func->paramTypes.size() &&
        !canImplicitlyConvert(lastExprType, func->paramTypes[i])) {
      addError("Function argument type mismatch in call to '" + node->funcName + "'");
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
  for (auto& stmt : node->stmts) {
    hasReturn = false;
    stmt->accept(this);
    if (hasReturn) blockReturns = true;
  }
  hasReturn = blockReturns;
  symbolTable.exitScope();
}

void SemanticAnalyzer::visit(EmptyStmt* node) {
  (void)node;
  hasReturn = false;
}

void SemanticAnalyzer::visit(ExprStmt* node) {
  node->expr->accept(this);
  hasReturn = false;
}

void SemanticAnalyzer::visit(AssignStmt* node) {
  Symbol* sym = symbolTable.lookupValue(node->varName);
  if (!sym) {
    addError("Variable '" + node->varName + "' used before declaration");
  } else if (sym->kind == SymbolKind::CONSTANT) {
    addError("Cannot assign to const variable '" + node->varName + "'");
  }

  node->value->accept(this);
  if (!sym) {
    hasReturn = false;
    return;
  }
  if (!isNumericType(lastExprType) || !canImplicitlyConvert(lastExprType, sym->type)) {
    addError("Assigned value type mismatch for '" + node->varName + "'");
  }
  hasReturn = false;
}

void SemanticAnalyzer::visitDeclDefs(VarDeclStmt* node) {
  for (auto& defPtr : node->defs) {
    VarDef* def = defPtr.get();
    bool initIsConst = false;
    ScalarValue constValue = ScalarValue::Int(0);

    if (node->declaredType.isConst && !def->hasInit) {
      addError("Const variable '" + def->name + "' must be initialized");
    }

    if (def->hasInit) {
      def->initExpr->accept(this);
      if (!isNumericType(lastExprType)) {
        addError("Initializer for '" + def->name + "' must be int or float expression");
      } else if (!canImplicitlyConvert(lastExprType, node->declaredType)) {
        addError("Initializer type mismatch for '" + def->name + "'");
      }
      initIsConst = evalConstExpr(def->initExpr.get(), &constValue);
      if (initIsConst) {
        constValue = castConstValue(constValue, node->declaredType);
      }
      if (node->declaredType.isConst && !initIsConst) {
        addError("Const variable '" + def->name + "' must use a constant expression initializer");
      }
      if (symbolTable.isGlobalScope() && !initIsConst) {
        addError("Global variable '" + def->name + "' must use a constant expression initializer");
      }
    }

    if (!symbolTable.declareValue(def->name, node->declaredType, false,
                                  node->declaredType.isConst && initIsConst,
                                  constValue)) {
      addError("Identifier '" + def->name + "' already declared in this scope");
      continue;
    }

    def->initIsConst = initIsConst;
    def->typedConstInitValue = constValue;
    if (constValue.isInt()) def->constInitValue = constValue.intValue;
    else def->constInitValue = static_cast<int>(constValue.floatValue);
  }
}

void SemanticAnalyzer::visit(VarDeclStmt* node) {
  visitDeclDefs(node);
  hasReturn = false;
}

void SemanticAnalyzer::visit(IfStmt* node) {
  node->condition->accept(this);
  if (!isNumericType(lastExprType)) {
    addError("If condition must be int or float expression");
  }

  bool thenHasReturn = false;
  hasReturn = false;
  node->thenStmt->accept(this);
  thenHasReturn = hasReturn;

  bool elseHasReturn = false;
  if (node->elseStmt) {
    hasReturn = false;
    node->elseStmt->accept(this);
    elseHasReturn = hasReturn;
  }

  hasReturn = thenHasReturn && elseHasReturn;
}

void SemanticAnalyzer::visit(WhileStmt* node) {
  node->condition->accept(this);
  if (!isNumericType(lastExprType)) {
    addError("While condition must be int or float expression");
  }

  loopDepth++;
  hasReturn = false;
  node->body->accept(this);
  loopDepth--;
  hasReturn = false;
}

void SemanticAnalyzer::visit(BreakStmt* node) {
  (void)node;
  if (loopDepth == 0) {
    addError("'break' statement not in loop");
  }
  hasReturn = false;
}

void SemanticAnalyzer::visit(ContinueStmt* node) {
  (void)node;
  if (loopDepth == 0) {
    addError("'continue' statement not in loop");
  }
  hasReturn = false;
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
}

void SemanticAnalyzer::visit(FuncDef* node) {
  currentFunction = node;
  hasReturn = false;
  symbolTable.enterScope();

  for (auto& param : node->params) {
    if (!symbolTable.declareValue(param->name, param->type, true, false,
                                  ScalarValue::Int(0))) {
      addError("Parameter '" + param->name + "' already declared");
    }
  }

  node->body->accept(this);

  if ((node->returnType.base == BaseType::INT ||
       node->returnType.base == BaseType::FLOAT) &&
      !hasReturn) {
    addError("Non-void function '" + node->name + "' must return a value on all execution paths");
  }

  symbolTable.exitScope();
  currentFunction = nullptr;
}

void SemanticAnalyzer::visit(CompUnit* node) {
  for (auto& item : node->items) {
    if (auto* decl = dynamic_cast<VarDeclStmt*>(item.get())) {
      decl->accept(this);
      continue;
    }
    if (auto* func = dynamic_cast<FuncDef*>(item.get())) {
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

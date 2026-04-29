#include "semantic_analyzer.h"

#include <sstream>

bool SemanticAnalyzer::isIntType(const Type& type) const {
  return type.base == BaseType::INT && !type.isArray;
}

void SemanticAnalyzer::setExprResult(const Type& type, bool isConst,
                                     int constValue) {
  lastExprType = type;
  lastExprIsConst = isConst;
  lastExprConstValue = constValue;
}

bool SemanticAnalyzer::evalConstExpr(Expr* expr, int* value) {
  if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
    *value = num->value;
    return true;
  }
  if (auto* id = dynamic_cast<IdentifierExpr*>(expr)) {
    Symbol* sym = symbolTable.lookupValue(id->name);
    if (!sym || !sym->hasConstValue) return false;
    *value = sym->constValue;
    return true;
  }
  if (auto* paren = dynamic_cast<ParenExpr*>(expr)) {
    return evalConstExpr(paren->expr.get(), value);
  }
  if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
    int operand = 0;
    if (!evalConstExpr(unary->operand.get(), &operand)) return false;
    switch (unary->op) {
      case UnaryExpr::U_PLUS:
        *value = operand;
        return true;
      case UnaryExpr::U_MINUS:
        *value = -operand;
        return true;
      case UnaryExpr::U_NOT:
        return false;
    }
  }
  if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
    int lhs = 0;
    int rhs = 0;
    if (!evalConstExpr(binary->left.get(), &lhs) ||
        !evalConstExpr(binary->right.get(), &rhs)) {
      return false;
    }
    switch (binary->op) {
      case BinaryExpr::B_ADD:
        *value = lhs + rhs;
        return true;
      case BinaryExpr::B_SUB:
        *value = lhs - rhs;
        return true;
      case BinaryExpr::B_MUL:
        *value = lhs * rhs;
        return true;
      case BinaryExpr::B_DIV:
        if (rhs == 0) return false;
        *value = lhs / rhs;
        return true;
      case BinaryExpr::B_MOD:
        if (rhs == 0) return false;
        *value = lhs % rhs;
        return true;
      default:
        return false;
    }
  }
  return false;
}

void SemanticAnalyzer::visit(NumberExpr* node) {
  setExprResult(Type::Int(), true, node->value);
}

void SemanticAnalyzer::visit(IdentifierExpr* node) {
  Symbol* sym = symbolTable.lookupValue(node->name);
  if (!sym) {
    addError("Identifier '" + node->name + "' used before declaration");
    setExprResult(Type::Invalid());
    return;
  }
  setExprResult(sym->type.withoutConst(), sym->hasConstValue, sym->constValue);
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
    if (!isIntType(lastExprType)) {
      addError("Function argument must be int expression");
    }
    if (i < func->paramTypes.size() &&
        !func->paramTypes[i].equalsIgnoringConst(lastExprType)) {
      addError("Function argument type mismatch in call to '" + node->funcName + "'");
    }
  }

  setExprResult(func->type.withoutConst(), false, 0);
}

void SemanticAnalyzer::visit(UnaryExpr* node) {
  node->operand->accept(this);
  if (!isIntType(lastExprType)) {
    addError("Unary operator requires int operand");
    setExprResult(Type::Invalid());
    return;
  }

  int constValue = 0;
  bool isConst = false;
  if (lastExprIsConst) {
    isConst = true;
    switch (node->op) {
      case UnaryExpr::U_PLUS:
        constValue = lastExprConstValue;
        break;
      case UnaryExpr::U_MINUS:
        constValue = -lastExprConstValue;
        break;
      case UnaryExpr::U_NOT:
        constValue = !lastExprConstValue;
        break;
    }
  }
  setExprResult(Type::Int(), isConst, constValue);
}

void SemanticAnalyzer::visit(BinaryExpr* node) {
  node->left->accept(this);
  Type lhsType = lastExprType;
  bool lhsConst = lastExprIsConst;
  int lhsValue = lastExprConstValue;
  if (!isIntType(lhsType)) {
    addError("Binary operator requires int operands");
  }

  node->right->accept(this);
  Type rhsType = lastExprType;
  bool rhsConst = lastExprIsConst;
  int rhsValue = lastExprConstValue;
  if (!isIntType(rhsType)) {
    addError("Binary operator requires int operands");
  }

  bool isConst = lhsConst && rhsConst;
  int constValue = 0;
  if (isConst) {
    switch (node->op) {
      case BinaryExpr::B_ADD:
        constValue = lhsValue + rhsValue;
        break;
      case BinaryExpr::B_SUB:
        constValue = lhsValue - rhsValue;
        break;
      case BinaryExpr::B_MUL:
        constValue = lhsValue * rhsValue;
        break;
      case BinaryExpr::B_DIV:
        if (rhsValue == 0) isConst = false;
        else constValue = lhsValue / rhsValue;
        break;
      case BinaryExpr::B_MOD:
        if (rhsValue == 0) isConst = false;
        else constValue = lhsValue % rhsValue;
        break;
      case BinaryExpr::B_LT:
        constValue = lhsValue < rhsValue;
        break;
      case BinaryExpr::B_GT:
        constValue = lhsValue > rhsValue;
        break;
      case BinaryExpr::B_LE:
        constValue = lhsValue <= rhsValue;
        break;
      case BinaryExpr::B_GE:
        constValue = lhsValue >= rhsValue;
        break;
      case BinaryExpr::B_EQ:
        constValue = lhsValue == rhsValue;
        break;
      case BinaryExpr::B_NE:
        constValue = lhsValue != rhsValue;
        break;
      case BinaryExpr::B_AND:
        constValue = (lhsValue && rhsValue) ? 1 : 0;
        break;
      case BinaryExpr::B_OR:
        constValue = (lhsValue || rhsValue) ? 1 : 0;
        break;
    }
  }

  setExprResult(Type::Int(), isConst, constValue);
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
  if (!isIntType(lastExprType)) {
    addError("Assigned value must be an int expression");
  }
  hasReturn = false;
}

void SemanticAnalyzer::visitDeclDefs(VarDeclStmt* node) {
  for (auto& defPtr : node->defs) {
    VarDef* def = defPtr.get();
    bool initIsConst = false;
    int constValue = 0;

    if (node->declaredType.isConst && !def->hasInit) {
      addError("Const variable '" + def->name + "' must be initialized");
    }

    if (def->hasInit) {
      def->initExpr->accept(this);
      if (!isIntType(lastExprType)) {
        addError("Initializer for '" + def->name + "' must be int expression");
      }
      initIsConst = evalConstExpr(def->initExpr.get(), &constValue);
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
    def->constInitValue = constValue;
  }
}

void SemanticAnalyzer::visit(VarDeclStmt* node) {
  visitDeclDefs(node);
  hasReturn = false;
}

void SemanticAnalyzer::visit(IfStmt* node) {
  node->condition->accept(this);
  if (!isIntType(lastExprType)) {
    addError("If condition must be int expression");
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
  if (!isIntType(lastExprType)) {
    addError("While condition must be int expression");
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
  } else if (!isIntType(lastExprType)) {
    addError("Non-void function '" + currentFunction->name + "' must return int expression");
  }
  hasReturn = true;
}

void SemanticAnalyzer::visit(FuncDef* node) {
  currentFunction = node;
  hasReturn = false;
  symbolTable.enterScope();

  for (auto& param : node->params) {
    if (!symbolTable.declareValue(param->name, param->type, true, false, 0)) {
      addError("Parameter '" + param->name + "' already declared");
    }
  }

  node->body->accept(this);

  if (node->returnType.base == BaseType::INT && !hasReturn) {
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

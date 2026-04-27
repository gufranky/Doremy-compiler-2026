#include "semantic_analyzer.h"

#include <sstream>

// Expression visitors
void SemanticAnalyzer::visit(NumberExpr* node) { lastExprIsVoid = false; }

void SemanticAnalyzer::visit(IdentifierExpr* node) {
  // Check if variable is declared
  Symbol* sym = symbolTable.lookupVariable(node->name);
  if (!sym) {
    addError("Variable '" + node->name + "' used before declaration");
  }
  lastExprIsVoid = false;
}

void SemanticAnalyzer::visit(ParenExpr* node) { node->expr->accept(this); }

void SemanticAnalyzer::visit(FunctionCallExpr* node) {
  // Check if function is declared
  Symbol* func = symbolTable.lookupFunction(node->funcName);
  if (!func) {
    addError("Function '" + node->funcName + "' not declared");
    lastExprIsVoid = false;
    return;
  }

  // Check parameter count
  if (func->paramCount != static_cast<int>(node->args.size())) {
    std::stringstream ss;
    ss << "Function '" << node->funcName << "' expects " << func->paramCount
       << " arguments, but " << node->args.size() << " provided";
    addError(ss.str());
  }

  // Check each argument
  for (auto& arg : node->args) {
    arg->accept(this);
    if (lastExprIsVoid) {
      addError("Cannot use void function call as an argument");
    }
  }

  lastExprIsVoid = func->isVoidReturn;
}

void SemanticAnalyzer::visit(UnaryExpr* node) {
  node->operand->accept(this);
  if (lastExprIsVoid) {
    addError("Cannot apply unary operator to void expression");
  }
  lastExprIsVoid = false;
}

void SemanticAnalyzer::visit(BinaryExpr* node) {
  node->left->accept(this);
  if (lastExprIsVoid) {
    addError("Cannot use void expression in binary operation");
  }

  node->right->accept(this);
  if (lastExprIsVoid) {
    addError("Cannot use void expression in binary operation");
  }

  lastExprIsVoid = false;
}

// Statement visitors
void SemanticAnalyzer::visit(Block* node) {
  symbolTable.enterScope();

  bool blockReturns = false;
  for (auto& stmt : node->stmts) {
    hasReturn = false;  // reset before each stmt to capture its own result
    stmt->accept(this);
    if (hasReturn) blockReturns = true;
  }

  hasReturn = blockReturns;
  symbolTable.exitScope();
}

void SemanticAnalyzer::visit(EmptyStmt* node) {
  // Nothing to check
  hasReturn = false;
}

void SemanticAnalyzer::visit(ExprStmt* node) {
  node->expr->accept(this);
  // Expression statements can have void expressions (e.g., void function calls)
  hasReturn = false;
}

void SemanticAnalyzer::visit(AssignStmt* node) {
  // Check if variable is declared
  Symbol* sym = symbolTable.lookupVariable(node->varName);
  if (!sym) {
    addError("Variable '" + node->varName + "' used before declaration");
  }

  // Check that the value is not void
  node->value->accept(this);
  if (lastExprIsVoid) {
    addError("Cannot assign void expression to variable");
  }

  hasReturn = false;
}

void SemanticAnalyzer::visit(VarDeclStmt* node) {
  // Check that variable name doesn't conflict
  if (!symbolTable.declareVariable(node->varName)) {
    addError("Variable '" + node->varName + "' already declared in this scope");
  }

  // Check initializer expression
  node->initExpr->accept(this);
  if (lastExprIsVoid) {
    addError("Cannot initialize variable with void expression");
  }

  hasReturn = false;
}

void SemanticAnalyzer::visit(IfStmt* node) {
  // Check condition
  node->condition->accept(this);
  if (lastExprIsVoid) {
    addError("Cannot use void expression as if condition");
  }

  // Check then branch
  bool thenHasReturn = false;
  hasReturn = false;
  node->thenStmt->accept(this);
  thenHasReturn = hasReturn;

  // Check else branch if exists
  bool elseHasReturn = false;
  if (node->elseStmt) {
    hasReturn = false;
    node->elseStmt->accept(this);
    elseHasReturn = hasReturn;
  }

  // Both branches must return for if to guarantee return
  hasReturn = thenHasReturn && elseHasReturn;
}

void SemanticAnalyzer::visit(WhileStmt* node) {
  // Check condition
  node->condition->accept(this);
  if (lastExprIsVoid) {
    addError("Cannot use void expression as while condition");
  }

  // Enter loop context
  loopDepth++;
  hasReturn = false;
  node->body->accept(this);
  loopDepth--;

  // While loops don't guarantee return (might not execute)
  hasReturn = false;
}

void SemanticAnalyzer::visit(BreakStmt* node) {
  if (loopDepth == 0) {
    addError("'break' statement not in loop");
  }

  hasReturn = false;
}

void SemanticAnalyzer::visit(ContinueStmt* node) {
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
    // return; form
    if (currentFunction->returnType != FuncDef::RT_VOID) {
      addError("Non-void function '" + currentFunction->name +
               "' must return a value");
    }
    hasReturn = true;
    return;
  }

  // Check return value expression
  node->returnValue->accept(this);

  if (currentFunction->returnType == FuncDef::RT_VOID) {
    addError("Void function '" + currentFunction->name +
             "' cannot return a value");
  } else {
    if (lastExprIsVoid) {
      addError("Cannot return void expression from int function");
    }
  }

  hasReturn = true;
}

void SemanticAnalyzer::visit(FuncDef* node) {
  currentFunction = node;
  hasReturn = false;

  // Function declaration is handled in CompUnit, just analyze body here

  // Enter function scope
  symbolTable.enterScope();

  // Declare parameters
  for (auto& param : node->params) {
    if (!symbolTable.declareVariable(param->name, true)) {
      addError("Parameter '" + param->name + "' already declared");
    }
  }

  // Analyze function body
  node->body->accept(this);

  // Check return requirements
  if (node->returnType == FuncDef::RT_INT && !hasReturn) {
    addError("Non-void function '" + node->name +
             "' must return a value on all execution paths");
  }

  // Exit function scope
  symbolTable.exitScope();

  currentFunction = nullptr;
}

void SemanticAnalyzer::visit(CompUnit* node) {
  // First pass: declare all functions
  for (auto& func : node->functions) {
    bool isVoid = (func->returnType == FuncDef::RT_VOID);
    if (!symbolTable.declareFunction(func->name, isVoid, func->params.size())) {
      addError("Function '" + func->name + "' already declared");
    }
  }

  // Second pass: analyze each function body
  for (auto& func : node->functions) {
    func->accept(this);
  }
}

#ifndef SEMANTIC_ANALYZER_H
#define SEMANTIC_ANALYZER_H

#include <string>
#include <vector>

#include "ast.h"
#include "symbol_table.h"

class SemanticAnalyzer : public ASTVisitor {
 private:
  SymbolTable symbolTable;
  std::vector<std::string> errors;

  // Context tracking
  int loopDepth;  // Track if we're inside a loop (for break/continue)
  FuncDef* currentFunction;  // Track current function being analyzed
  bool hasReturn;            // Track if current path has a return statement

  // Type tracking for expressions
  bool lastExprIsVoid;  // True if last expression was a void function call

  void addError(const std::string& msg) { errors.push_back(msg); }

 public:
  SemanticAnalyzer()
      : loopDepth(0),
        currentFunction(nullptr),
        hasReturn(false),
        lastExprIsVoid(false) {}

  // Run semantic analysis on the AST
  bool analyze(CompUnit* root) {
    errors.clear();
    root->accept(this);

    // Check for main function
    if (!symbolTable.hasMainFunction()) {
      addError(
          "Program must have a 'main' function with signature: int main()");
    }

    return errors.empty();
  }

  const std::vector<std::string>& getErrors() const { return errors; }

  // Visitor methods
  void visit(NumberExpr* node) override;
  void visit(IdentifierExpr* node) override;
  void visit(ParenExpr* node) override;
  void visit(FunctionCallExpr* node) override;
  void visit(UnaryExpr* node) override;
  void visit(BinaryExpr* node) override;

  void visit(Block* node) override;
  void visit(EmptyStmt* node) override;
  void visit(ExprStmt* node) override;
  void visit(AssignStmt* node) override;
  void visit(VarDeclStmt* node) override;
  void visit(IfStmt* node) override;
  void visit(WhileStmt* node) override;
  void visit(BreakStmt* node) override;
  void visit(ContinueStmt* node) override;
  void visit(ReturnStmt* node) override;

  void visit(FuncDef* node) override;
  void visit(CompUnit* node) override;
};

#endif  // SEMANTIC_ANALYZER_H

#ifndef SEMANTIC_ANALYZER_H
#define SEMANTIC_ANALYZER_H

#include <string>
#include <vector>

#include "ast.h"
#include "symbol_table.h"

// 前向声明
class SemanticAnalyzer;

// 辅助结构：左值分析结果
struct LValueInfo {
  std::string name;           // 变量名
  Symbol* symbol = nullptr;   // 符号信息
  Type type;                  // 类型（已减少维度）
  bool isValid = false;       // 是否有效
  bool isConst = false;       // 是否常量
  std::vector<Expr*> indices; // 索引表达式列表
};

class SemanticAnalyzer : public ASTVisitor {
 private:
  SymbolTable symbolTable;
  std::vector<std::string> errors;
  int loopDepth = 0;
  FuncDef* currentFunction = nullptr;
  bool hasReturn = false;

  Type lastExprType = Type::Invalid();
  bool lastExprIsConst = false;
  ScalarValue lastExprConstValue = ScalarValue::Int(0);

  void addError(const std::string& msg) { errors.push_back(msg); }
  void declareBuiltinFunctions();
  bool isIntType(const Type& type) const;
  bool isFloatType(const Type& type) const;
  bool isNumericType(const Type& type) const;
  bool isNumericOrArrayBase(const Type& type) const;
  bool canImplicitlyConvert(const Type& from, const Type& to) const;
  bool isParamCompatible(const Type& argType, const Type& paramType) const;
  ScalarValue castConstValue(const ScalarValue& value, const Type& targetType) const;
  bool isTruthy(const ScalarValue& value) const;
  Type commonNumericType(const Type& lhs, const Type& rhs) const;
  void setExprResult(const Type& type, bool isConst = false,
                     ScalarValue constValue = ScalarValue::Int(0));
  bool evalConstExpr(Expr* expr, ScalarValue* value);
  void visitDeclDefs(VarDeclStmt* node);

  // 数组相关辅助方法
  LValueInfo analyzeLValue(Expr* expr);
  bool validateArrayDimensions(VarDef* def);
  bool validateInitList(InitList* initList, const Type& arrayType,
                        std::vector<ScalarValue>* flattenedValues);
  void flattenInitList(InitList* initList, const Type& elemType,
                       std::vector<ScalarValue>& result, int& index, int totalSize);

 public:
  SemanticAnalyzer() = default;

  bool analyze(CompUnit* root) {
    errors.clear();
    symbolTable.reset();
    declareBuiltinFunctions();
    loopDepth = 0;
    currentFunction = nullptr;
    hasReturn = false;
    lastExprType = Type::Invalid();
    lastExprIsConst = false;
    lastExprConstValue = ScalarValue::Int(0);

    root->accept(this);
    if (!symbolTable.hasMainFunction()) {
      addError("Program must have a 'main' function with signature: int main()");
    }
    return errors.empty();
  }

  const std::vector<std::string>& getErrors() const { return errors; }

  void visit(NumberExpr* node) override;
  void visit(IdentifierExpr* node) override;
  void visit(ParenExpr* node) override;
  void visit(FunctionCallExpr* node) override;
  void visit(UnaryExpr* node) override;
  void visit(BinaryExpr* node) override;
  void visit(ArrayAccessExpr* node) override;
  void visit(InitList* node) override;

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

#endif

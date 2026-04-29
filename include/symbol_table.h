#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ast.h"

enum class SymbolKind { VARIABLE, CONSTANT, FUNCTION };

struct Symbol {
  std::string name;
  SymbolKind kind;
  Type type;
  bool isParameter = false;
  bool isGlobal = false;
  bool hasConstValue = false;
  int constValue = 0;
  std::vector<Type> paramTypes;

  Symbol(const std::string& n, SymbolKind k, const Type& t)
      : name(n), kind(k), type(t) {}
};

class SymbolTable {
 private:
  std::vector<std::map<std::string, std::unique_ptr<Symbol>>> valueScopes;
  std::map<std::string, std::unique_ptr<Symbol>> functionTable;

 public:
  SymbolTable() { reset(); }

  void reset() {
    valueScopes.clear();
    functionTable.clear();
    enterScope();
  }

  void enterScope() { valueScopes.emplace_back(); }

  void exitScope() {
    if (valueScopes.size() > 1) {
      valueScopes.pop_back();
    }
  }

  int getScopeLevel() const { return static_cast<int>(valueScopes.size()); }
  bool isGlobalScope() const { return valueScopes.size() == 1; }

  bool declareValue(const std::string& name, const Type& type,
                    bool isParam = false, bool hasConstValue = false,
                    int constValue = 0) {
    if (valueScopes.empty()) return false;
    auto& currentScope = valueScopes.back();
    if (currentScope.find(name) != currentScope.end()) return false;
    if (isGlobalScope() && functionTable.find(name) != functionTable.end()) {
      return false;
    }

    SymbolKind kind = type.isConst ? SymbolKind::CONSTANT : SymbolKind::VARIABLE;
    auto symbol = std::make_unique<Symbol>(name, kind, type);
    symbol->isParameter = isParam;
    symbol->isGlobal = isGlobalScope();
    symbol->hasConstValue = hasConstValue;
    symbol->constValue = constValue;
    currentScope[name] = std::move(symbol);
    return true;
  }

  Symbol* lookupValue(const std::string& name) {
    for (auto it = valueScopes.rbegin(); it != valueScopes.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) return found->second.get();
    }
    return nullptr;
  }

  Symbol* lookupGlobalValue(const std::string& name) {
    if (valueScopes.empty()) return nullptr;
    auto found = valueScopes.front().find(name);
    if (found == valueScopes.front().end()) return nullptr;
    return found->second.get();
  }

  bool declareFunction(const std::string& name, const Type& returnType,
                       const std::vector<Type>& paramTypes) {
    if (functionTable.find(name) != functionTable.end()) return false;
    if (!valueScopes.empty() && valueScopes.front().find(name) != valueScopes.front().end()) {
      return false;
    }

    auto symbol = std::make_unique<Symbol>(name, SymbolKind::FUNCTION, returnType);
    symbol->isGlobal = true;
    symbol->paramTypes = paramTypes;
    functionTable[name] = std::move(symbol);
    return true;
  }

  Symbol* lookupFunction(const std::string& name) {
    auto found = functionTable.find(name);
    if (found != functionTable.end()) return found->second.get();
    return nullptr;
  }

  bool hasMainFunction() const {
    auto it = functionTable.find("main");
    if (it == functionTable.end()) return false;
    const Symbol* mainSym = it->second.get();
    return mainSym->type.base == BaseType::INT && !mainSym->type.isArray &&
           mainSym->paramTypes.empty();
  }
};

#endif

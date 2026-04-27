#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <map>
#include <memory>
#include <string>
#include <vector>

// Symbol types
enum class SymbolType { VARIABLE, FUNCTION };

// Symbol information
struct Symbol {
  std::string name;
  SymbolType type;

  // For variables
  bool isParameter;

  // For functions
  bool isVoidReturn;
  int paramCount;

  Symbol(const std::string& n, SymbolType t)
      : name(n),
        type(t),
        isParameter(false),
        isVoidReturn(false),
        paramCount(0) {}
};

// Symbol table with scope management
class SymbolTable {
 private:
  // Each scope is a map from name to symbol
  std::vector<std::map<std::string, std::unique_ptr<Symbol>>> scopes;

  // Global function table (functions are only in global scope)
  std::map<std::string, std::unique_ptr<Symbol>> functionTable;

 public:
  SymbolTable() {
    // Start with global scope
    enterScope();
  }

  // Scope management
  void enterScope() { scopes.emplace_back(); }

  void exitScope() {
    if (scopes.size() > 1) {  // Keep at least global scope
      scopes.pop_back();
    }
  }

  int getScopeLevel() const { return scopes.size(); }

  // Variable operations
  bool declareVariable(const std::string& name, bool isParam = false) {
    if (scopes.empty()) return false;

    // Check if variable already exists in current scope
    auto& currentScope = scopes.back();
    if (currentScope.find(name) != currentScope.end()) {
      return false;  // Variable already declared in this scope
    }

    auto symbol = std::make_unique<Symbol>(name, SymbolType::VARIABLE);
    symbol->isParameter = isParam;
    currentScope[name] = std::move(symbol);
    return true;
  }

  Symbol* lookupVariable(const std::string& name) {
    // Search from innermost to outermost scope
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) {
        return found->second.get();
      }
    }
    return nullptr;
  }

  // Function operations
  bool declareFunction(const std::string& name, bool isVoid, int paramCount) {
    // Check if function already exists
    if (functionTable.find(name) != functionTable.end()) {
      return false;  // Function already declared
    }

    auto symbol = std::make_unique<Symbol>(name, SymbolType::FUNCTION);
    symbol->isVoidReturn = isVoid;
    symbol->paramCount = paramCount;
    functionTable[name] = std::move(symbol);
    return true;
  }

  Symbol* lookupFunction(const std::string& name) {
    auto found = functionTable.find(name);
    if (found != functionTable.end()) {
      return found->second.get();
    }
    return nullptr;
  }

  bool hasMainFunction() const {
    auto it = functionTable.find("main");
    if (it == functionTable.end()) return false;

    // Check that main returns int and has no parameters
    const Symbol* mainSym = it->second.get();
    return !mainSym->isVoidReturn && mainSym->paramCount == 0;
  }
};

#endif  // SYMBOL_TABLE_H

#include <cassert>
#include <cstdio>
#include <iostream>

#include "ast.h"
#include "backend_codegen.h"
#include "ir_generator.h"
#include "optimizer.h"
#include "semantic_analyzer.h"

using namespace ir;

extern FILE* yyin;
extern int yyparse();
extern CompUnit* root;

int main() {
  // Test with a simple function
  const char* testcase = "testcases/functional/f03_if_else.c";

  yyin = fopen(testcase, "r");
  assert(yyin && "failed to open testcase");
  int parseRet = yyparse();
  assert(parseRet == 0 && root);

  SemanticAnalyzer sema;
  bool ok = sema.analyze(root);
  assert(ok);

  IRGenerator gen;
  IRProgram prog = gen.generate(root);

  delete root;
  root = nullptr;
  fclose(yyin);

  // Optimize
  OptimizeProgram(prog);

  // Generate assembly with register allocation
  CodeGen cg;
  std::vector<std::string> asmLines = cg.generate(prog);

  std::cout << "=== Generated Assembly with Register Allocation ===\n";
  std::cout << "Test: " << testcase << "\n\n";

  for (const auto& line : asmLines) {
    std::cout << line << "\n";
  }

  return 0;
}

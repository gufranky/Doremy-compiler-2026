#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

#include "ast.h"
#include "backend_codegen.h"
#include "ir_generator.h"
#include "optimizer.h"
#include "semantic_analyzer.h"

using namespace ir;

extern FILE* yyin;
extern int yyparse();
extern CompUnit* root;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: asm_single <cfile>\n";
    return 1;
  }
  std::string testcase = argv[1];
  yyin = fopen(testcase.c_str(), "r");
  if (!yyin) {
    std::cerr << "failed to open " << testcase << "\n";
    return 1;
  }
  int parseRet = yyparse();
  if (parseRet != 0 || !root) {
    std::cerr << "parse failed\n";
    return 1;
  }

  SemanticAnalyzer sema;
  bool ok = sema.analyze(root);
  assert(ok);

  IRGenerator gen;
  IRProgram prog = gen.generate(root);

  delete root;
  root = nullptr;
  fclose(yyin);
  yyin = nullptr;

  OptimizeProgram(prog);

  CodeGen cg;
  auto asmLines = cg.generate(prog);
  for (const auto& line : asmLines) {
    std::cout << line << "\n";
  }
  return 0;
}

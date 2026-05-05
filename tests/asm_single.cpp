#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

#include "ast.h"
#include "backend_codegen.h"
#include "ir_generator.h"
#include "optimizer.h"
#include "parse_driver.h"
#include "semantic_analyzer.h"

using namespace ir;

extern CompUnit* root;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: asm_single <cfile>\n";
    return 1;
  }
  std::string testcase = argv[1];
  bool parsed = frontend::parse_from_file(testcase);
  if (!parsed || !root) {
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

  OptimizeProgram(prog);

  CodeGen cg;
  auto asmLines = cg.generate(prog);
  for (const auto& line : asmLines) {
    std::cout << line << "\n";
  }
  return 0;
}

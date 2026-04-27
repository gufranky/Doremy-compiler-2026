#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ast.h"
#include "backend_codegen.h"
#include "ir_generator.h"
#include "optimizer.h"
#include "semantic_analyzer.h"

using namespace ir;

extern FILE* yyin;
extern int yyparse();
extern CompUnit* root;

static IRProgram buildProgramFromFile(const std::string& path) {
  yyin = fopen(path.c_str(), "r");
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
  yyin = nullptr;
  return prog;
}

static std::vector<std::string> collectTestcases() {
  std::vector<std::string> files;
  for (auto& entry :
       std::filesystem::directory_iterator("testcases/functional")) {
    if (entry.is_regular_file()) files.push_back(entry.path().string());
  }
  std::sort(files.begin(), files.end());
  return files;
}

int main() {
  auto files = collectTestcases();
  CodeGen cg;
  std::cout << "Backend B2 over " << files.size() << " testcases\n";
  for (const auto& testcase : files) {
    IRProgram prog = buildProgramFromFile(testcase);
    OptimizeProgram(prog);
    std::vector<std::string> asmLines = cg.generate(prog);

    std::cout << "Generated optimized assembly (B2) for " << testcase << ":\n";
    for (const auto& line : asmLines) {
      std::cout << line << "\n";
    }

    bool hasMain = false;
    bool hasRet = false;
    for (const auto& line : asmLines) {
      if (line.find("main:") != std::string::npos) hasMain = true;
      if (line.find("ret") != std::string::npos) hasRet = true;
    }
    assert(hasMain && hasRet);
  }

  return 0;
}

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

#include "ast.h"
#include "cfg.h"
#include "ir.h"
#include "ir_generator.h"
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
  std::cout << "CFG build over " << files.size() << " testcases\n";
  for (const auto& path : files) {
    IRProgram prog = buildProgramFromFile(path);
    size_t funcCount = 0;
    for (auto& fn : prog.functions) {
      ControlFlowGraph cfg = ControlFlowGraph::Build(&fn);
      assert(cfg.entry != nullptr);
      assert(!cfg.blocks.empty());
      funcCount++;
    }
    std::cout << "  ok: " << path << " (functions=" << funcCount << ")\n";
  }
  return 0;
}

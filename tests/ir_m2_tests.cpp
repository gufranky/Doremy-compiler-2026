#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

#include "ast.h"
#include "ir.h"
#include "ir_generator.h"
#include "parse_driver.h"
#include "semantic_analyzer.h"

using namespace ir;

extern CompUnit* root;

static IRProgram buildProgramFromFile(const std::string& path) {
  bool parsed = frontend::parse_from_file(path);
  assert(parsed && root);

  SemanticAnalyzer sema;
  bool ok = sema.analyze(root);
  assert(ok);

  IRGenerator gen;
  IRProgram prog = gen.generate(root);

  delete root;
  root = nullptr;
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
  std::cout << "IR gen over " << files.size() << " testcases\n";
  for (const auto& path : files) {
    IRProgram prog = buildProgramFromFile(path);
    assert(!prog.functions.empty());
    size_t totalInst = 0;
    for (const auto& fn : prog.functions) totalInst += fn.instructions.size();
    assert(totalInst > 0);
    std::cout << "  ok: " << path << " (functions=" << prog.functions.size()
              << ", insts=" << totalInst << ")\n";
  }
  return 0;
}

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ast.h"
#include "backend_liveness.h"
#include "backend_regalloc.h"
#include "ir_generator.h"
#include "semantic_analyzer.h"

using namespace ir;
using namespace regalloc;

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
  std::cout << "Register Allocation (B3) over " << files.size()
            << " testcases\n";

  int totalFunctions = 0;
  int totalAllocated = 0;
  int functionsWithSpills = 0;

  for (const auto& testcase : files) {
    IRProgram prog = buildProgramFromFile(testcase);

    for (auto& fn : prog.functions) {
      totalFunctions++;

      // Perform liveness analysis
      LivenessResult lr = AnalyzeLiveness(fn);

      // Perform register allocation
      GraphColoringAllocator allocator;
      RegAllocResult result = allocator.allocate(fn, lr);

      totalAllocated += result.allocation.size();

      if (result.needsRetry || !result.spilledVRegs.empty()) {
        functionsWithSpills++;
      }
    }

    std::cout << "  ok: " << testcase << "\n";
  }

  std::cout << "\nSummary:\n";
  std::cout << "  Total functions: " << totalFunctions << "\n";
  std::cout << "  Total virtual registers allocated: " << totalAllocated
            << "\n";
  std::cout << "  Functions requiring spills: " << functionsWithSpills << "\n";
  std::cout << "  Success rate: "
            << (100.0 * (totalFunctions - functionsWithSpills) / totalFunctions)
            << "%\n";

  return 0;
}

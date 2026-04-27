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
  std::cout << "CodeGen (B4) with register allocation over " << files.size()
            << " testcases\n";

  CodeGen cg;

  for (const auto& testcase : files) {
    IRProgram prog = buildProgramFromFile(testcase);

    // Optionally optimize
    OptimizeProgram(prog);

    // Generate assembly with register allocation and stack frames
    std::vector<std::string> asmLines = cg.generate(prog);

    bool hasMain = false;
    bool hasRet = false;
    bool hasPrologue = false;
    bool hasEpilogue = false;

    for (const auto& line : asmLines) {
      if (line.find("main:") != std::string::npos) hasMain = true;
      if (line.find("ret") != std::string::npos) hasRet = true;
      if (line.find("addi sp, sp, -") != std::string::npos) hasPrologue = true;
      if (line.find("addi sp, sp,") != std::string::npos &&
          line.find("-") == std::string::npos)
        hasEpilogue = true;
    }

    assert(hasMain && "Missing main function");
    assert(hasRet && "Missing return instruction");

    std::cout << "  ok: " << testcase << " (lines=" << asmLines.size()
              << ", prologue=" << (hasPrologue ? "yes" : "no")
              << ", epilogue=" << (hasEpilogue ? "yes" : "no") << ")\n";
  }

  std::cout << "\nAll B4 tests passed!\n";
  return 0;
}

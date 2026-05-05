#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "ast.h"
#include "backend_codegen.h"
#include "ir_generator.h"
#include "optimizer.h"
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

static bool isInt12(int value) { return value >= -2048 && value <= 2047; }

static void assertSpImmediateRange(const std::vector<std::string>& asmLines,
                                   const std::string& testcase) {
  const std::regex spAddiPattern(R"(addi\s+sp,\s*sp,\s*(-?\d+))");
  const std::regex spMemPattern(R"((?:lw|sw)\s+[^,]+,\s*(-?\d+)\(sp\))");
  std::smatch match;

  for (const auto& line : asmLines) {
    if (std::regex_search(line, match, spAddiPattern)) {
      int imm = std::stoi(match[1].str());
      assert(isInt12(imm) && "sp addi immediate out of int12 range");
    }
    if (std::regex_search(line, match, spMemPattern)) {
      int imm = std::stoi(match[1].str());
      assert(isInt12(imm) && "sp memory immediate out of int12 range");
    }
  }
}

static void assertManyParamsPositiveLargeOffset(
    const std::vector<std::string>& asmLines, const std::string& testcase) {
  if (testcase.find("32_many_params3") == std::string::npos) return;

  bool hasPositive2048Materialization = false;
  for (size_t i = 0; i + 2 < asmLines.size(); ++i) {
    if (asmLines[i].find("lui ") != std::string::npos &&
        asmLines[i + 1].find("addi ") != std::string::npos &&
        asmLines[i + 1].find(", 2048") != std::string::npos &&
        asmLines[i + 2].find("add ") != std::string::npos &&
        asmLines[i + 2].find(", sp,") != std::string::npos) {
      hasPositive2048Materialization = true;
    }

    bool hasNegative2048Materialization =
        asmLines[i].find("lui ") != std::string::npos &&
        asmLines[i + 1].find("addi ") != std::string::npos &&
        asmLines[i + 1].find(", -2048") != std::string::npos &&
        asmLines[i + 2].find("add ") != std::string::npos &&
        asmLines[i + 2].find(", sp,") != std::string::npos;
    assert(!hasNegative2048Materialization &&
           "many-params large positive offset materialized as -2048");
  }

  assert(hasPositive2048Materialization &&
         "missing positive 2048 stack address materialization");
}

static void assertManyParamsCurrentValueSlots(
    const std::vector<std::string>& asmLines, const std::string& testcase) {
  if (testcase.find("32_many_params3") == std::string::npos) return;

  bool storesOriginalParamSlot = false;
  bool storesCurrentValueSlot = false;
  bool loadsCurrentValueSlot = false;

  for (const auto& line : asmLines) {
    if (line.find("sw ") != std::string::npos &&
        line.find(", 836(sp)") != std::string::npos) {
      storesOriginalParamSlot = true;
    }
    if (line.find("sw ") != std::string::npos &&
        line.find(", 840(sp)") != std::string::npos) {
      storesCurrentValueSlot = true;
    }
    if (line.find("lw ") != std::string::npos &&
        line.find(", 840(sp)") != std::string::npos) {
      loadsCurrentValueSlot = true;
    }
  }

  assert(storesOriginalParamSlot &&
         "missing stable incoming parameter slot store for ib");
  assert(storesCurrentValueSlot &&
         "missing separate current-value slot store for ib");
  assert(loadsCurrentValueSlot &&
         "missing current-value reload for rewritten ib");
}

static void assertManyParamsOutgoingArgSeparation(
    const std::vector<std::string>& asmLines, const std::string& testcase) {
  if (testcase.find("32_many_params3") == std::string::npos) return;

  bool hasCallArgStore4804 = false;
  bool hasCallArgStore6500 = false;
  bool hasCallArgStore6544 = false;
  bool hasCallerSlotReload6500 = false;
  bool hasCallerSlotReload6544 = false;

  for (size_t i = 0; i < asmLines.size(); ++i) {
    const auto& line = asmLines[i];
    if (line.find("4804") != std::string::npos) hasCallArgStore4804 = true;
    if (line.find("6500") != std::string::npos) hasCallArgStore6500 = true;
    if (line.find("6544") != std::string::npos) hasCallArgStore6544 = true;

    if (i + 2 < asmLines.size() && asmLines[i].find("6500") != std::string::npos &&
        asmLines[i + 1].find(", sp,") != std::string::npos &&
        asmLines[i + 2].find("lw ") != std::string::npos) {
      hasCallerSlotReload6500 = true;
    }
    if (i + 2 < asmLines.size() && asmLines[i].find("6544") != std::string::npos &&
        asmLines[i + 1].find(", sp,") != std::string::npos &&
        asmLines[i + 2].find("lw ") != std::string::npos) {
      hasCallerSlotReload6544 = true;
    }
  }

  assert(hasCallArgStore4804 && "missing high-offset outgoing arg materialization");
  assert(hasCallArgStore6500 && "missing yj outgoing arg materialization");
  assert(hasCallArgStore6544 && "missing yu outgoing arg materialization");
  assert(hasCallerSlotReload6500 && "missing direct caller-slot reload for yj");
  assert(hasCallerSlotReload6544 && "missing direct caller-slot reload for yu");
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

    assertSpImmediateRange(asmLines, testcase);
    assertManyParamsPositiveLargeOffset(asmLines, testcase);
    assertManyParamsCurrentValueSlots(asmLines, testcase);
    assertManyParamsOutgoingArgSeparation(asmLines, testcase);

    assert(hasMain && "Missing main function");
    assert(hasRet && "Missing return instruction");

    std::cout << "  ok: " << testcase << " (lines=" << asmLines.size()
              << ", prologue=" << (hasPrologue ? "yes" : "no")
              << ", epilogue=" << (hasEpilogue ? "yes" : "no") << ")\n";
  }

  std::cout << "\nAll B4 tests passed!\n";
  return 0;
}

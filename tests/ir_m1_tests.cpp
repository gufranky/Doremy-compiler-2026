#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ast.h"
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
  // Operand creation
  Operand imm = Operand::Imm(-7);
  assert(imm.isImm());
  assert(!imm.isVReg());
  assert(imm.immValue == -7);

  Operand v0 = Operand::VReg(0);
  assert(v0.isVReg());
  assert(v0.vregId == 0);

  Operand g = Operand::Global("gVar");
  assert(g.isGlobal());
  assert(g.globalName == "gVar");

  // Function vreg numbering and instruction appends
  IRFunction fn("test");
  int r1 = fn.newVReg();
  int r2 = fn.newVReg();
  assert(r1 == 0 && r2 == 1);

  auto* b = fn.append<BinaryInst>(BinaryOp::Add, r1, Operand::Imm(1),
                                  Operand::Imm(2));
  assert(fn.instructions.size() == 1);
  assert(b->dest == r1 && b->op == BinaryOp::Add);

  auto* u = fn.append<UnaryInst>(UnaryOp::Neg, r2, Operand::VReg(r1));
  assert(fn.instructions.size() == 2);
  assert(u->dest == r2 && u->op == UnaryOp::Neg);

  auto* c = fn.append<CallInst>(r2, "callee",
                                std::vector<Operand>{Operand::VReg(r1)});
  assert(c->hasDest);
  assert(c->dest == r2);
  assert(c->args.size() == 1);

  auto* br = fn.append<BranchInst>(Operand::VReg(r2), "L_true", "L_false");
  assert(br->trueLabel == "L_true");
  assert(br->falseLabel == "L_false");

  fn.append<LabelInst>("L_end");
  fn.append<ReturnInst>(Operand::VReg(r2));
  assert(fn.instructions.back()->kind == InstKind::Return);

  // Run IR creation over all functional testcases
  auto files = collectTestcases();
  std::cout << "IR m1 invariants over " << files.size() << " testcases\n";
  for (const auto& path : files) {
    IRProgram prog = buildProgramFromFile(path);
    assert(!prog.functions.empty());
    size_t totalInst = 0;
    size_t returnCount = 0;
    for (const auto& f : prog.functions) {
      totalInst += f.instructions.size();
      for (const auto& inst : f.instructions) {
        if (inst->kind == InstKind::Return) returnCount++;
      }
    }
    assert(totalInst > 0);
    assert(returnCount > 0);
    std::cout << "  ok: " << path << " (functions=" << prog.functions.size()
              << ", insts=" << totalInst << ", returns=" << returnCount
              << ")\n";
  }

  return 0;
}

#include <cassert>
#include <iostream>

#include "ir.h"
#include "midir.h"

using namespace ir;

static IRProgram buildBranchingProgram() {
  IRProgram program;
  IRFunction fn("branching");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 4;

  fn.append<LabelInst>("entry");
  fn.append<BranchInst>(Operand::VReg(0), "then", "else");

  fn.append<LabelInst>("then");
  fn.append<CopyInst>(ValueType::I32, 1, Operand::Imm(1));
  fn.append<JumpInst>("merge");

  fn.append<LabelInst>("else");
  fn.append<CopyInst>(ValueType::I32, 2, Operand::Imm(2));
  fn.append<JumpInst>("merge");

  fn.append<LabelInst>("merge");
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 3,
                        Operand::VReg(1), Operand::VReg(2));
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(3));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildLoopProgram() {
  IRProgram program;
  IRFunction fn("looping");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 4;

  fn.append<LabelInst>("entry");
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("header");
  fn.append<LoadInst>(ValueType::I32, 1, Operand::LocalVarAddr(0));
  fn.append<BranchInst>(Operand::VReg(0), "body", "exit");

  fn.append<LabelInst>("body");
  fn.append<StoreInst>(ValueType::I32, Operand::VReg(1), Operand::LocalVarAddr(0));
  fn.append<CallInst>(ValueType::I32, 2, "getint", std::vector<Operand>{},
                      std::vector<ValueType>{});
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("exit");
  fn.append<ReturnInst>(ValueType::I32, Operand::Imm(0));

  program.functions.push_back(std::move(fn));
  return program;
}

int main() {
  {
    IRProgram program = buildBranchingProgram();
    midir::Module module = midir::BuildMidIR(program);
    assert(module.functions.size() == 1);
    const auto& fn = module.functions.front();
    assert(fn.blocks.size() == 4);
    assert(fn.entryBlock == 0);
    assert(fn.blocks[0].succs.size() == 2);
    assert(fn.blocks[3].preds.size() == 2);

    std::string error;
    assert(midir::VerifyMidIR(module, &error));

    midir::DominatorTree dom = midir::BuildDominatorTree(fn);
    assert(dom.idom.size() == fn.blocks.size());
    assert(dom.idom[0] == 0);
    assert(dom.idom[3] == 0);
    assert(dom.frontier[1].size() == 1 && dom.frontier[1][0] == 3);
    assert(dom.frontier[2].size() == 1 && dom.frontier[2][0] == 3);

    IRProgram lowered = midir::LowerMidToLIR(module);
    assert(lowered.functions.size() == 1);
    assert(lowered.functions.front().instructions.size() ==
           program.functions.front().instructions.size());
  }

  {
    IRProgram program = buildLoopProgram();
    midir::Module module = midir::BuildMidIR(program);
    assert(module.functions.size() == 1);
    auto& fn = module.functions.front();

    midir::ConvertToSSA(fn);
    assert(fn.inSSA);

    midir::DominatorTree dom = midir::BuildDominatorTree(fn);
    midir::LoopInfo loops = midir::BuildLoopInfo(fn, dom);
    assert(!loops.loops.empty());
    assert(loops.loops.front().header == 1);
    assert(loops.loops.front().latch == 2);

    midir::MemorySSA mssa = midir::BuildMemorySSA(fn, dom);
    bool sawLoadUse = false;
    bool sawStoreDef = false;
    bool sawMergePhi = false;
    for (const auto& access : mssa.accesses) {
      if (access.kind == midir::MemoryAccess::Kind::Use && access.block == 1) {
        sawLoadUse = true;
      }
      if (access.kind == midir::MemoryAccess::Kind::Def && access.block == 2) {
        sawStoreDef = true;
      }
      if (access.kind == midir::MemoryAccess::Kind::Phi && access.block == 1) {
        sawMergePhi = true;
      }
    }
    assert(sawLoadUse);
    assert(sawStoreDef);
    assert(sawMergePhi);
  }

  std::cout << "midir tests passed\n";
  return 0;
}

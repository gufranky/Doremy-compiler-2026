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

static IRProgram buildMem2RegProgram() {
  IRProgram program;
  IRFunction fn("mem2reg");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 3;

  fn.append<LabelInst>("entry");
  fn.append<BranchInst>(Operand::VReg(0), "then", "else");

  fn.append<LabelInst>("then");
  fn.append<StoreInst>(ValueType::I32, Operand::Imm(1), Operand::LocalVarAddr(0));
  fn.append<JumpInst>("merge");

  fn.append<LabelInst>("else");
  fn.append<StoreInst>(ValueType::I32, Operand::Imm(2), Operand::LocalVarAddr(0));
  fn.append<JumpInst>("merge");

  fn.append<LabelInst>("merge");
  fn.append<LoadInst>(ValueType::I32, 1, Operand::LocalVarAddr(0));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 2,
                        Operand::VReg(1), Operand::Imm(3));
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(2));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildLoopProgram() {
  IRProgram program;
  program.globals.emplace_back("gCounter", ScalarValue::Int(0), false);
  program.globals.back().valueType = ValueType::I32;
  program.globals.back().typedInitialValue = ScalarValue::Int(0);

  IRFunction fn("looping");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 4;

  fn.append<LabelInst>("entry");
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("header");
  fn.append<LoadInst>(ValueType::I32, 1, Operand::Global("gCounter"));
  fn.append<BranchInst>(Operand::VReg(0), "body", "exit");

  fn.append<LabelInst>("body");
  fn.append<StoreInst>(ValueType::I32, Operand::VReg(1), Operand::Global("gCounter"));
  fn.append<CallInst>(ValueType::I32, 2, "getint", std::vector<Operand>{},
                      std::vector<ValueType>{});
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("exit");
  fn.append<ReturnInst>(ValueType::I32, Operand::Imm(0));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildParallelCopyProgram() {
  IRProgram program;
  IRFunction fn("parallel_copy");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 3;

  fn.append<LabelInst>("entry");
  fn.append<BranchInst>(Operand::VReg(0), "left", "right");

  fn.append<LabelInst>("left");
  fn.append<StoreInst>(ValueType::I32, Operand::Imm(10), Operand::LocalVarAddr(0));
  fn.append<StoreInst>(ValueType::I32, Operand::Imm(20), Operand::LocalVarAddr(8));
  fn.append<JumpInst>("merge");

  fn.append<LabelInst>("right");
  fn.append<StoreInst>(ValueType::I32, Operand::Imm(20), Operand::LocalVarAddr(0));
  fn.append<StoreInst>(ValueType::I32, Operand::Imm(10), Operand::LocalVarAddr(8));
  fn.append<JumpInst>("merge");

  fn.append<LabelInst>("merge");
  fn.append<LoadInst>(ValueType::I32, 1, Operand::LocalVarAddr(0));
  fn.append<LoadInst>(ValueType::I32, 2, Operand::LocalVarAddr(8));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 3,
                        Operand::VReg(1), Operand::VReg(2));
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(3));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildUnsafeFrameProgram() {
  IRProgram program;
  IRFunction fn("unsafe_frame");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 3;

  fn.append<LabelInst>("entry");
  fn.append<StoreInst>(ValueType::I32, Operand::Imm(1), Operand::LocalVarAddr(0));
  fn.append<CopyInst>(ValueType::I32, 1, Operand::LocalVarAddr(0));
  fn.append<LoadInst>(ValueType::I32, 2, Operand::LocalVarAddr(0));
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(2));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildDeadAfterTerminatorProgram() {
  IRProgram program;
  IRFunction fn("dead_after_term");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 3;

  fn.append<LabelInst>("entry");
  fn.append<BranchInst>(Operand::VReg(0), "then", "else");
  fn.append<CopyInst>(ValueType::I32, 1, Operand::Imm(7));

  fn.append<LabelInst>("then");
  fn.append<ReturnInst>(ValueType::I32, Operand::Imm(1));
  fn.append<CopyInst>(ValueType::I32, 2, Operand::Imm(99));

  fn.append<LabelInst>("else");
  fn.append<ReturnInst>(ValueType::I32, Operand::Imm(2));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildParamMutationProgram() {
  IRProgram program;
  IRFunction fn("param_mutation");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 4;
  fn.params.push_back(0);
  fn.paramTypes.push_back(ValueType::I32);
  fn.paramIsArray.push_back(false);

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::I32, 0, Operand::Imm(4));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 1,
                        Operand::VReg(0), Operand::Imm(5));
  fn.append<BinaryInst>(BinaryOp::Mul, ValueType::I32, ValueType::I32, 2,
                        Operand::VReg(1), Operand::Imm(2));
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(2));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildLoopInvariantLoadProgram() {
  IRProgram program;
  program.globals.emplace_back("gA", ScalarValue::Int(7), false);
  program.globals.back().valueType = ValueType::I32;
  program.globals.back().typedInitialValue = ScalarValue::Int(7);

  IRFunction fn("loop_invariant_load");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 6;

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::I32, 0, Operand::Imm(0));
  fn.append<CopyInst>(ValueType::I32, 1, Operand::Imm(0));
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("header");
  fn.append<BinaryInst>(BinaryOp::Lt, ValueType::I32, ValueType::I32, 2,
                        Operand::VReg(0), Operand::Imm(4));
  fn.append<BranchInst>(Operand::VReg(2), "body", "exit");

  fn.append<LabelInst>("body");
  fn.append<LoadInst>(ValueType::I32, 3, Operand::Global("gA"));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 4,
                        Operand::VReg(1), Operand::VReg(3));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 1,
                        Operand::VReg(4), Operand::Imm(0));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 0,
                        Operand::VReg(0), Operand::Imm(1));
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("exit");
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(1));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildLoopClobberedLoadProgram() {
  IRProgram program;
  program.globals.emplace_back("gA", ScalarValue::Int(7), false);
  program.globals.back().valueType = ValueType::I32;
  program.globals.back().typedInitialValue = ScalarValue::Int(7);

  IRFunction fn("loop_clobbered_load");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 5;

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::I32, 0, Operand::Imm(0));
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("header");
  fn.append<BinaryInst>(BinaryOp::Lt, ValueType::I32, ValueType::I32, 1,
                        Operand::VReg(0), Operand::Imm(4));
  fn.append<BranchInst>(Operand::VReg(1), "body", "exit");

  fn.append<LabelInst>("body");
  fn.append<LoadInst>(ValueType::I32, 2, Operand::Global("gA"));
  fn.append<StoreInst>(ValueType::I32, Operand::VReg(0), Operand::Global("gA"));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 0,
                        Operand::VReg(0), Operand::Imm(1));
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("exit");
  fn.append<ReturnInst>(ValueType::I32, Operand::Imm(0));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildLoopDisjointOffsetLoadProgram() {
  IRProgram program;
  program.globals.emplace_back("gArrBase", ScalarValue::Int(7), false);
  program.globals.back().valueType = ValueType::I32;
  program.globals.back().typedInitialValue = ScalarValue::Int(7);
  IRFunction fn("loop_disjoint_offset_load");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 8;

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::I32, 0, Operand::Imm(0));
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("header");
  fn.append<BinaryInst>(BinaryOp::Lt, ValueType::I32, ValueType::I32, 1,
                        Operand::VReg(0), Operand::Imm(4));
  fn.append<BranchInst>(Operand::VReg(1), "body", "exit");

  fn.append<LabelInst>("body");
  fn.append<LoadInst>(ValueType::I32, 2, Operand::Global("gArrBase"));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 3,
                        Operand::Global("gArrBase"), Operand::Imm(32));
  fn.append<StoreInst>(ValueType::I32, Operand::VReg(0), Operand::VReg(3));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 0,
                        Operand::VReg(0), Operand::Imm(1));
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("exit");
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(2));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildLoopNeedPreheaderProgram() {
  IRProgram program;
  IRFunction fn("loop_need_preheader");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 5;

  fn.append<LabelInst>("entry");
  fn.append<BranchInst>(Operand::VReg(0), "setup1", "setup2");

  fn.append<LabelInst>("setup1");
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("setup2");
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("header");
  fn.append<BinaryInst>(BinaryOp::Lt, ValueType::I32, ValueType::I32, 1,
                        Operand::VReg(2), Operand::Imm(4));
  fn.append<BranchInst>(Operand::VReg(1), "body", "exit");

  fn.append<LabelInst>("body");
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 2,
                        Operand::VReg(2), Operand::Imm(1));
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("exit");
  fn.append<ReturnInst>(ValueType::I32, Operand::Imm(0));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildLCSSAProgram() {
  IRProgram program;
  IRFunction fn("lcssa");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 5;

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::I32, 0, Operand::Imm(0));
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("header");
  fn.append<BinaryInst>(BinaryOp::Lt, ValueType::I32, ValueType::I32, 1,
                        Operand::VReg(0), Operand::Imm(4));
  fn.append<BranchInst>(Operand::VReg(1), "body", "exit");

  fn.append<LabelInst>("body");
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 2,
                        Operand::VReg(0), Operand::Imm(10));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 0,
                        Operand::VReg(0), Operand::Imm(1));
  fn.append<JumpInst>("header");

  fn.append<LabelInst>("exit");
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 3,
                        Operand::VReg(2), Operand::Imm(1));
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(3));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildAddressCanonicalizationProgram() {
  IRProgram program;
  IRFunction fn("addr_canonical");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 4;

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::I32, 0, Operand::LocalVarAddr(0));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 1,
                        Operand::LocalVarAddr(8), Operand::Imm(16));
  fn.append<LoadInst>(ValueType::I32, 2, Operand::VReg(1));
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(2));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildGEPCanonicalizationProgram() {
  IRProgram program;
  IRFunction fn("gep_canonical");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 6;

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::I32, 0, Operand::Imm(3));
  fn.append<BinaryInst>(BinaryOp::Mul, ValueType::I32, ValueType::I32, 1,
                        Operand::VReg(0), Operand::Imm(4));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 2,
                        Operand::LocalVarAddr(32), Operand::VReg(1));
  fn.append<LoadInst>(ValueType::I32, 3, Operand::VReg(2));
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(3));

  program.functions.push_back(std::move(fn));
  return program;
}

static IRProgram buildNestedGEPCanonicalizationProgram() {
  IRProgram program;
  IRFunction fn("nested_gep_canonical");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 8;

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::I32, 0, Operand::Imm(3));
  fn.append<BinaryInst>(BinaryOp::Mul, ValueType::I32, ValueType::I32, 1,
                        Operand::VReg(0), Operand::Imm(4));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 2,
                        Operand::LocalVarAddr(32), Operand::VReg(1));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 3,
                        Operand::VReg(2), Operand::Imm(8));
  fn.append<LoadInst>(ValueType::I32, 4, Operand::VReg(3));
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(4));

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
    IRProgram program = buildMem2RegProgram();
    midir::Module module = midir::BuildMidIR(program);
    auto& fn = module.functions.front();
    midir::ConvertToSSA(fn);

    bool sawPhi = false;
    bool sawLoad = false;
    bool sawStore = false;
    for (const auto& block : fn.blocks) {
      for (const auto& inst : block.instructions) {
        sawPhi = sawPhi || inst.kind == midir::InstKind::Phi;
        sawLoad = sawLoad || inst.kind == midir::InstKind::Load;
        sawStore = sawStore || inst.kind == midir::InstKind::Store;
      }
    }
    assert(sawPhi);
    assert(!sawLoad);
    assert(!sawStore);

    std::string error;
    assert(midir::VerifyMidIR(module, &error));

    IRProgram lowered = midir::LowerMidToLIR(module);
    bool loweredHasPhiLikeLoadStore = false;
    for (const auto& inst : lowered.functions.front().instructions) {
      if (inst->kind == InstKind::Load || inst->kind == InstKind::Store) {
        loweredHasPhiLikeLoadStore = true;
      }
    }
    assert(!loweredHasPhiLikeLoadStore);
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

  {
    IRProgram program = buildParallelCopyProgram();
    midir::Module module = midir::BuildMidIR(program);
    auto& fn = module.functions.front();
    midir::ConvertToSSA(fn);

    bool sawPhi = false;
    for (const auto& block : fn.blocks) {
      for (const auto& inst : block.instructions) {
        if (inst.kind == midir::InstKind::Phi) sawPhi = true;
      }
    }
    assert(sawPhi);

    IRProgram lowered = midir::LowerMidToLIR(module);
    bool sawTempCopy = false;
    for (const auto& inst : lowered.functions.front().instructions) {
      if (inst->kind != InstKind::Copy) continue;
      auto* copy = static_cast<CopyInst*>(inst.get());
      if (copy->dest >= 4) {
        sawTempCopy = true;
        break;
      }
    }
    assert(sawTempCopy);
  }

  {
    IRProgram program = buildUnsafeFrameProgram();
    midir::Module module = midir::BuildMidIR(program);
    auto& fn = module.functions.front();
    midir::ConvertToSSA(fn);

    bool sawLoad = false;
    bool sawStore = false;
    for (const auto& block : fn.blocks) {
      for (const auto& inst : block.instructions) {
        sawLoad = sawLoad || inst.kind == midir::InstKind::Load;
        sawStore = sawStore || inst.kind == midir::InstKind::Store;
      }
    }
    assert(sawLoad);
    assert(sawStore);
  }

  {
    IRProgram program = buildAddressCanonicalizationProgram();
    midir::Module module = midir::BuildMidIR(program);
    midir::OptimizeMidProgram(module);
    std::string error;
    assert(midir::VerifyMidIR(module, &error));

    IRProgram lowered = midir::LowerMidToLIR(module);
    assert(!lowered.functions.empty());
  }

  {
    IRProgram program = buildGEPCanonicalizationProgram();
    midir::Module module = midir::BuildMidIR(program);
    midir::OptimizeMidProgram(module);
    std::string error;
    assert(midir::VerifyMidIR(module, &error));

    bool sawGEP = false;
    for (const auto& block : module.functions.front().blocks) {
      for (const auto& inst : block.instructions) {
        if (inst.kind == midir::InstKind::GEP) {
          sawGEP = true;
          assert(inst.operands.size() == 1);
          assert(inst.operands[0].isRegister());
          assert(inst.gepIndex.isRegister() || inst.gepIndex.isConstInt());
        }
      }
    }
    assert(sawGEP);
  }

  {
    IRProgram program = buildNestedGEPCanonicalizationProgram();
    midir::Module module = midir::BuildMidIR(program);
    midir::OptimizeMidProgram(module);
    std::string error;
    assert(midir::VerifyMidIR(module, &error));

    bool sawNestedFoldedGEP = false;
    bool sawPtrAdd = false;
    for (const auto& block : module.functions.front().blocks) {
      for (const auto& inst : block.instructions) {
        if (inst.kind == midir::InstKind::GEP && inst.gepOffset == 8) {
          sawNestedFoldedGEP = true;
        }
        if (inst.kind == midir::InstKind::Binary && inst.type.kind == midir::TypeKind::Ptr) {
          sawPtrAdd = true;
        }
      }
    }
    assert(sawNestedFoldedGEP);
    assert(!sawPtrAdd);
  }

  {
    midir::Function fn;
    fn.name = "invalid_ptr_ir";
    fn.entryBlock = 0;
    fn.nextValueId = 3;
    fn.values.resize(3);

    midir::BasicBlock block;
    block.name = "entry";

    midir::Instruction badCopy;
    badCopy.kind = midir::InstKind::Copy;
    badCopy.type = midir::Type::Ptr();
    badCopy.dest = 0;
    badCopy.hasResult = true;
    badCopy.operands.push_back(midir::Operand::ConstInt(4));
    block.instructions.push_back(std::move(badCopy));

    midir::Instruction badAdd;
    badAdd.kind = midir::InstKind::Binary;
    badAdd.binaryOp = ir::BinaryOp::Add;
    badAdd.type = midir::Type::Ptr();
    badAdd.dest = 1;
    badAdd.hasResult = true;
    badAdd.operands.push_back(midir::Operand::Reg(0, midir::Type::Ptr()));
    badAdd.operands.push_back(midir::Operand::Global("gA"));
    block.instructions.push_back(std::move(badAdd));

    midir::Instruction ret;
    ret.kind = midir::InstKind::Return;
    ret.type = midir::Type::Void();
    ret.hasValue = false;
    block.instructions.push_back(std::move(ret));

    fn.blocks.push_back(std::move(block));
    midir::Module module;
    module.functions.push_back(std::move(fn));

    std::string error;
    assert(!midir::VerifyMidIR(module, &error));
    assert(!error.empty());
  }

  {
    IRProgram program = buildDeadAfterTerminatorProgram();
    midir::Module module = midir::BuildMidIR(program);
    std::string error;
    assert(midir::VerifyMidIR(module, &error));

    const auto& fn = module.functions.front();
    assert(fn.blocks.size() == 3);
    assert(fn.blocks[0].instructions.back().kind == midir::InstKind::Branch);
    assert(fn.blocks[1].instructions.back().kind == midir::InstKind::Return);
    assert(fn.blocks[2].instructions.back().kind == midir::InstKind::Return);
    assert(fn.blocks[1].instructions.size() == 1);
  }

  {
    IRProgram program = buildParamMutationProgram();
    midir::Module module = midir::BuildMidIR(program);
    auto& fn = module.functions.front();
    midir::ConvertToSSA(fn);

    int phiCount = 0;
    int copyCount = 0;
    for (const auto& block : fn.blocks) {
      for (const auto& inst : block.instructions) {
        if (inst.kind == midir::InstKind::Phi) ++phiCount;
        if (inst.kind == midir::InstKind::Copy) ++copyCount;
      }
    }
    assert(phiCount == 0);
    assert(copyCount == 1);
  }

  {
    IRProgram program = buildLoopInvariantLoadProgram();
    midir::Module module = midir::BuildMidIR(program);
    auto& fn = module.functions.front();

    bool sawLoadInLoopBodyBefore = false;
    for (const auto& inst : fn.blocks[2].instructions) {
      if (inst.kind == midir::InstKind::Load) sawLoadInLoopBodyBefore = true;
    }
    assert(sawLoadInLoopBodyBefore);

    midir::OptimizeMidProgram(module);

    bool sawLoadInLoopBodyAfter = false;
    bool sawLoadInPreheader = false;
    for (const auto& inst : module.functions.front().blocks[0].instructions) {
      if (inst.kind == midir::InstKind::Load) sawLoadInPreheader = true;
    }
    for (const auto& inst : module.functions.front().blocks[2].instructions) {
      if (inst.kind == midir::InstKind::Load) sawLoadInLoopBodyAfter = true;
    }
    assert(sawLoadInPreheader);
    assert(!sawLoadInLoopBodyAfter);
  }

  {
    IRProgram program = buildLoopClobberedLoadProgram();
    midir::Module module = midir::BuildMidIR(program);
    midir::OptimizeMidProgram(module);

    bool sawLoadInLoopBody = false;
    for (const auto& inst : module.functions.front().blocks[2].instructions) {
      if (inst.kind == midir::InstKind::Load) sawLoadInLoopBody = true;
    }
    assert(sawLoadInLoopBody);
  }

  {
    IRProgram program = buildLoopDisjointOffsetLoadProgram();
    midir::Module module = midir::BuildMidIR(program);
    midir::OptimizeMidProgram(module);

    bool sawLoadInLoopBody = false;
    bool sawLoadInPreheader = false;
    for (const auto& inst : module.functions.front().blocks[0].instructions) {
      if (inst.kind == midir::InstKind::Load) sawLoadInPreheader = true;
    }
    for (const auto& inst : module.functions.front().blocks[2].instructions) {
      if (inst.kind == midir::InstKind::Load) sawLoadInLoopBody = true;
    }
    assert(sawLoadInPreheader);
    assert(!sawLoadInLoopBody);
  }

  {
    IRProgram program = buildLoopNeedPreheaderProgram();
    midir::Module module = midir::BuildMidIR(program);
    midir::OptimizeMidProgram(module);

    bool sawPreheader = false;
    for (const auto& block : module.functions.front().blocks) {
      if (block.name.find("header_preheader") != std::string::npos) {
        sawPreheader = true;
        break;
      }
    }
    assert(sawPreheader);
  }

  {
    IRProgram program = buildLCSSAProgram();
    midir::Module module = midir::BuildMidIR(program);
    midir::OptimizeMidProgram(module);

    const auto& fn = module.functions.front();
    int exitBlockIndex = -1;
    for (size_t i = 0; i < fn.blocks.size(); ++i) {
      if (fn.blocks[i].name == "exit") {
        exitBlockIndex = static_cast<int>(i);
        break;
      }
    }
    assert(exitBlockIndex >= 0);

    bool sawExitPhi = false;
    for (const auto& inst : fn.blocks[exitBlockIndex].instructions) {
      if (inst.kind == midir::InstKind::Phi) {
        sawExitPhi = true;
        break;
      }
    }
    assert(sawExitPhi);
  }

  std::cout << "midir tests passed\n";
  return 0;
}

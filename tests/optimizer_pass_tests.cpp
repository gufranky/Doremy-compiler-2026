#include <cassert>
#include <iostream>
#include <vector>

#include "ir.h"
#include "optimizer.h"

using namespace ir;

namespace {

OptimizeConfig onlyPass(OptPass pass) {
  OptimizeConfig config;
  config.passMask = optPassBit(pass);
  return config;
}

IRFunction buildCrossBlockCopyPropagationFunction() {
  IRFunction fn("cross_block_copy");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 5;
  fn.params.push_back(0);
  fn.paramTypes.push_back(ValueType::I32);
  fn.paramIsArray.push_back(false);

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::I32, 1, Operand::VReg(0));
  fn.append<BranchInst>(Operand::VReg(0), "then", "else");

  fn.append<LabelInst>("then");
  fn.append<JumpInst>("merge");

  fn.append<LabelInst>("else");
  fn.append<JumpInst>("merge");

  fn.append<LabelInst>("merge");
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::I32, ValueType::I32, 2,
                        Operand::VReg(1), Operand::Imm(4));
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(2));
  return fn;
}

IRFunction buildFloatBranchSimplifyFunction() {
  IRFunction fn("float_branch");
  fn.returnType = ValueType::F32;
  fn.nextVReg = 4;
  fn.paramTypes.push_back(ValueType::F32);
  fn.params.push_back(0);
  fn.paramIsArray.push_back(false);

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::I32, 2, Operand::Imm(1));
  fn.append<BranchInst>(Operand::VReg(2), "taken", "dead");

  fn.append<LabelInst>("taken");
  fn.append<CopyInst>(ValueType::F32, 1, Operand::VReg(0, ValueType::F32));
  fn.append<ReturnInst>(ValueType::F32, Operand::VReg(1, ValueType::F32));

  fn.append<LabelInst>("dead");
  fn.append<CopyInst>(ValueType::F32, 2, Operand::VReg(0, ValueType::F32));
  fn.append<ReturnInst>(ValueType::F32, Operand::VReg(2, ValueType::F32));
  return fn;
}

IRFunction buildDeadStoreFunction() {
  IRFunction fn("dead_store");
  fn.returnType = ValueType::I32;
  fn.nextVReg = 2;
  fn.append<LabelInst>("entry");
  fn.append<StoreInst>(ValueType::I32, Operand::Imm(1), Operand::LocalVarAddr(0));
  fn.append<StoreInst>(ValueType::I32, Operand::Imm(2), Operand::LocalVarAddr(0));
  fn.append<LoadInst>(ValueType::I32, 0, Operand::LocalVarAddr(0));
  fn.append<ReturnInst>(ValueType::I32, Operand::VReg(0));
  return fn;
}

IRFunction buildFloatBlockGvnFunction() {
  IRFunction fn("float_gvn");
  fn.returnType = ValueType::F32;
  fn.nextVReg = 4;
  fn.paramTypes.push_back(ValueType::F32);
  fn.params.push_back(0);
  fn.paramIsArray.push_back(false);

  fn.append<LabelInst>("entry");
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::F32, ValueType::F32, 1,
                        Operand::VReg(0, ValueType::F32), Operand::Imm(1.0f));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::F32, ValueType::F32, 2,
                        Operand::VReg(0, ValueType::F32), Operand::Imm(1.0f));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::F32, ValueType::F32, 3,
                        Operand::VReg(1, ValueType::F32), Operand::VReg(2, ValueType::F32));
  fn.append<ReturnInst>(ValueType::F32, Operand::VReg(3, ValueType::F32));
  return fn;
}

IRFunction buildFloatCommonSubexprFunction() {
  IRFunction fn("float_cse");
  fn.returnType = ValueType::F32;
  fn.nextVReg = 4;
  fn.paramTypes.push_back(ValueType::F32);
  fn.params.push_back(0);
  fn.paramIsArray.push_back(false);

  fn.append<LabelInst>("entry");
  fn.append<BinaryInst>(BinaryOp::Mul, ValueType::F32, ValueType::F32, 1,
                        Operand::Imm(2.0f), Operand::VReg(0, ValueType::F32));
  fn.append<BinaryInst>(BinaryOp::Mul, ValueType::F32, ValueType::F32, 2,
                        Operand::VReg(0, ValueType::F32), Operand::Imm(2.0f));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::F32, ValueType::F32, 3,
                        Operand::VReg(1, ValueType::F32), Operand::VReg(2, ValueType::F32));
  fn.append<ReturnInst>(ValueType::F32, Operand::VReg(3, ValueType::F32));
  return fn;
}

IRFunction buildFloatCastCopyFunction() {
  IRFunction fn("float_cast_copy");
  fn.returnType = ValueType::F32;
  fn.nextVReg = 4;
  fn.params.push_back(0);
  fn.paramTypes.push_back(ValueType::I32);
  fn.paramIsArray.push_back(false);

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::F32, 1, Operand::VReg(0, ValueType::I32));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::F32, ValueType::F32, 2,
                        Operand::VReg(1, ValueType::F32), Operand::Imm(1.0f));
  fn.append<ReturnInst>(ValueType::F32, Operand::VReg(2, ValueType::F32));
  return fn;
}

IRFunction buildFloatCopyPropagationFunction() {
  IRFunction fn("float_copy_prop");
  fn.returnType = ValueType::F32;
  fn.nextVReg = 4;
  fn.params.push_back(0);
  fn.paramTypes.push_back(ValueType::F32);
  fn.paramIsArray.push_back(false);

  fn.append<LabelInst>("entry");
  fn.append<CopyInst>(ValueType::F32, 1, Operand::VReg(0, ValueType::F32));
  fn.append<BinaryInst>(BinaryOp::Add, ValueType::F32, ValueType::F32, 2,
                        Operand::VReg(1, ValueType::F32), Operand::Imm(1.0f));
  fn.append<ReturnInst>(ValueType::F32, Operand::VReg(2, ValueType::F32));
  return fn;
}

}  // namespace

int main() {
  {
    IRFunction fn = buildCrossBlockCopyPropagationFunction();
    OptimizeFunction(fn, onlyPass(OptPass::CopyPropagate));

    bool sawRewrittenUse = false;
    for (const auto& inst : fn.instructions) {
      auto* bin = dynamic_cast<BinaryInst*>(inst.get());
      if (!bin) continue;
      if (bin->dest == 2) {
        assert(bin->lhs.isVReg());
        assert(bin->lhs.vregId == 0);
        sawRewrittenUse = true;
      }
    }
    assert(sawRewrittenUse);
  }

  {
    IRFunction fn = buildFloatBranchSimplifyFunction();
    OptimizeConfig config;
    config.passMask = optPassBit(OptPass::CopyPropagate) |
                      optPassBit(OptPass::DeadCodeElim) |
                      optPassBit(OptPass::SimplifyCFG);
    OptimizeFunction(fn, config);

    int branchCount = 0;
    int returnCount = 0;
    bool branchUsesImm = false;
    for (const auto& inst : fn.instructions) {
      if (auto* br = dynamic_cast<BranchInst*>(inst.get())) {
        ++branchCount;
        branchUsesImm = br->cond.isImm() && br->cond.immValue == 1;
      }
      if (dynamic_cast<ReturnInst*>(inst.get())) ++returnCount;
    }

    assert(branchCount == 1);
    assert(branchUsesImm);
    assert(returnCount == 2);
  }

  {
    IRFunction fn = buildDeadStoreFunction();
    OptimizeFunction(fn, onlyPass(OptPass::DeadStoreElim));

    int storeCount = 0;
    int loadCount = 0;
    for (const auto& inst : fn.instructions) {
      if (dynamic_cast<StoreInst*>(inst.get())) ++storeCount;
      if (dynamic_cast<LoadInst*>(inst.get())) ++loadCount;
    }

    assert(storeCount == 1);
    assert(loadCount == 1);
  }

  {
    IRFunction fn = buildFloatCopyPropagationFunction();
    OptimizeFunction(fn, onlyPass(OptPass::CopyPropagate));

    bool sawRewrittenUse = false;
    for (const auto& inst : fn.instructions) {
      if (auto* bin = dynamic_cast<BinaryInst*>(inst.get())) {
        if (bin->dest == 2) {
          sawRewrittenUse = bin->lhs.isVReg() && bin->lhs.vregId == 0 &&
                            bin->lhs.valueType == ValueType::F32;
        }
      }
    }

    assert(sawRewrittenUse);
  }

  {
    IRFunction fn = buildFloatCastCopyFunction();
    OptimizeFunction(fn, onlyPass(OptPass::CopyPropagate));

    bool sawUnchangedCast = false;
    for (const auto& inst : fn.instructions) {
      if (auto* bin = dynamic_cast<BinaryInst*>(inst.get())) {
        if (bin->dest == 2) {
          sawUnchangedCast = bin->lhs.isVReg() && bin->lhs.vregId == 1 &&
                             bin->lhs.valueType == ValueType::F32;
        }
      }
    }

    assert(sawUnchangedCast);
  }

  std::cout << "optimizer pass tests ok\n";
  return 0;
}

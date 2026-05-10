#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <vector>

namespace midir {

namespace {

struct InductionVariableInfo {
  int phi_value = -1;
  int update_block = -1;
  int update_result = -1;
  int step = 0;
};


std::optional<int> getConstantInt(const ValueRef& value) {
  if (value.kind == ValueRef::Kind::ImmediateInt) return value.int_value;
  return std::nullopt;
}

const Instruction* findDefInstruction(const Function& function, int valueId,
                                      int& blockIndexOut) {
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size());
       ++blockIndex) {
    for (const auto& inst : function.blocks[blockIndex].instructions) {
      if (inst.has_result && inst.result_id == valueId) {
        blockIndexOut = blockIndex;
        return &inst;
      }
    }
  }
  blockIndexOut = -1;
  return nullptr;
}

std::optional<InductionVariableInfo> matchInductionVariable(const Function& function,
                                                            const Loop& loop) {
  if (!hasSingleLatch(loop)) return std::nullopt;
  const int preheader = getLoopPreheader(function, loop);
  if (preheader < 0) return std::nullopt;
  const int latch = loop.latches.front();
  const auto& header = function.blocks[loop.header];

  for (const auto& inst : header.instructions) {
    if (inst.kind != InstKind::Phi) break;
    if (!inst.has_result || inst.result_id < 0 || inst.incomings.size() != 2 ||
        inst.result_type != Type::I32()) {
      continue;
    }

    const PhiIncoming* initIncoming = nullptr;
    const PhiIncoming* latchIncoming = nullptr;
    for (const auto& incoming : inst.incomings) {
      if (incoming.pred_block == preheader) {
        initIncoming = &incoming;
      } else if (incoming.pred_block == latch) {
        latchIncoming = &incoming;
      }
    }
    if (initIncoming == nullptr || latchIncoming == nullptr) continue;
    if (!latchIncoming->value.isSSA()) continue;

    int updateBlock = -1;
    const Instruction* updateInst =
        findDefInstruction(function, latchIncoming->value.value_id, updateBlock);
    if (updateInst == nullptr || updateBlock != latch) continue;
    if (updateInst->kind != InstKind::Binary || !updateInst->has_result ||
        updateInst->operands.size() != 2 || updateInst->operand_type != Type::I32() ||
        updateInst->result_type != Type::I32()) {
      continue;
    }

    int step = 0;
    if (updateInst->binary_op == ir::BinaryOp::Add) {
      if (updateInst->operands[0].isSSA() &&
          updateInst->operands[0].value_id == inst.result_id) {
        auto constant = getConstantInt(updateInst->operands[1]);
        if (!constant.has_value()) continue;
        step = *constant;
      } else if (updateInst->operands[1].isSSA() &&
                 updateInst->operands[1].value_id == inst.result_id) {
        auto constant = getConstantInt(updateInst->operands[0]);
        if (!constant.has_value()) continue;
        step = *constant;
      } else {
        continue;
      }
    } else if (updateInst->binary_op == ir::BinaryOp::Sub) {
      if (!(updateInst->operands[0].isSSA() &&
            updateInst->operands[0].value_id == inst.result_id)) {
        continue;
      }
      auto constant = getConstantInt(updateInst->operands[1]);
      if (!constant.has_value()) continue;
      step = -*constant;
    } else {
      continue;
    }

    (void)initIncoming;
    return InductionVariableInfo{inst.result_id, updateBlock, updateInst->result_id,
                                 step};
  }

  return std::nullopt;
}

void rewriteAsIvPlusConstant(Instruction& inst, int baseValue, int delta) {
  if (delta == 0) {
    inst.kind = InstKind::Copy;
    inst.binary_op = ir::BinaryOp::Add;
    inst.operand_type = Type::Void();
    inst.operands = {ValueRef::SSA(baseValue, Type::I32())};
    return;
  }

  inst.kind = InstKind::Binary;
  inst.binary_op = ir::BinaryOp::Add;
  inst.operand_type = Type::I32();
  inst.operands = {ValueRef::SSA(baseValue, Type::I32()),
                   ValueRef::ImmediateInt(delta)};
}

bool simplifyDerivedUsers(Function& function, const Loop& loop,
                          const InductionVariableInfo& info) {
  bool changed = false;

  for (int blockIndex : loop.blocks) {
    auto& block = function.blocks[blockIndex];
    for (auto& inst : block.instructions) {
      if (!inst.has_result || inst.result_id == info.update_result) continue;

      if (inst.kind == InstKind::Copy && inst.operands.size() == 1 &&
          inst.result_type == Type::I32() && inst.operands[0].isSSA() &&
          inst.operands[0].value_id == info.update_result) {
        rewriteAsIvPlusConstant(inst, info.phi_value, info.step);
        changed = true;
        continue;
      }

      if (inst.kind != InstKind::Binary || inst.operands.size() != 2 ||
          inst.result_type != Type::I32() || inst.operand_type != Type::I32()) {
        continue;
      }

      if (inst.binary_op == ir::BinaryOp::Add) {
        if (inst.operands[0].isSSA() && inst.operands[0].value_id == info.update_result) {
          auto c = getConstantInt(inst.operands[1]);
          if (c.has_value()) {
            rewriteAsIvPlusConstant(inst, info.phi_value, info.step + *c);
            changed = true;
            continue;
          }
        }
        if (inst.operands[1].isSSA() && inst.operands[1].value_id == info.update_result) {
          auto c = getConstantInt(inst.operands[0]);
          if (c.has_value()) {
            rewriteAsIvPlusConstant(inst, info.phi_value, info.step + *c);
            changed = true;
            continue;
          }
        }
        continue;
      }

      if (inst.binary_op == ir::BinaryOp::Sub && inst.operands[0].isSSA() &&
          inst.operands[0].value_id == info.update_result) {
        auto c = getConstantInt(inst.operands[1]);
        if (c.has_value()) {
          rewriteAsIvPlusConstant(inst, info.phi_value, info.step - *c);
          changed = true;
        }
      }
    }
  }

  return changed;
}

}  // namespace

std::string IndVarSimplifyPass::name() const { return "indvar-simplify"; }

PassResult IndVarSimplifyPass::run(Function& function,
                                   AnalysisManager& analysisManager) {
  rebuildEdges(function);
  const LoopInfo& loopInfo = analysisManager.getLoopInfo(function);

  std::vector<int> order(loopInfo.loops.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
    return loopInfo.loops[lhs].depth > loopInfo.loops[rhs].depth;
  });

  bool changed = false;
  for (int loopIndex : order) {
    auto info = matchInductionVariable(function, loopInfo.loops[loopIndex]);
    if (!info.has_value()) continue;
    changed = simplifyDerivedUsers(function, loopInfo.loops[loopIndex], *info) || changed;
  }

  if (changed) {
    rebuildEdges(function);
    normalizePhiIncomings(function);
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

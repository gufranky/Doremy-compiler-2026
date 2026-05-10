#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>
#include <vector>

namespace midir {

namespace {

bool loopContainsBlock(const Loop& loop, int block) {
  return std::binary_search(loop.blocks.begin(), loop.blocks.end(), block);
}


bool dominatesAllLoopUses(const Function& function, const Loop& loop,
                          const DominatorTree& domTree, int valueId,
                          int hoistBlock) {
  if (valueId < 0) return false;
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size());
       ++blockIndex) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& inst : block.instructions) {
      if (inst.kind == InstKind::Phi) {
        for (const auto& incoming : inst.incomings) {
          if (!incoming.value.isSSA() || incoming.value.value_id != valueId) continue;
          if (!loopContainsBlock(loop, incoming.pred_block)) continue;
          if (incoming.pred_block != hoistBlock &&
              !dominates(domTree, hoistBlock, incoming.pred_block)) {
            return false;
          }
        }
        continue;
      }

      bool used = std::any_of(inst.operands.begin(), inst.operands.end(),
                              [&](const ValueRef& operand) {
                                return operand.isSSA() && operand.value_id == valueId;
                              });
      if (!used || !loopContainsBlock(loop, blockIndex)) continue;
      if (blockIndex != hoistBlock && !dominates(domTree, hoistBlock, blockIndex)) {
        return false;
      }
    }
  }
  return true;
}

bool isLoopInvariantOperand(const ValueRef& operand, const Loop& loop,
                            const std::vector<int>& defBlock,
                            const std::unordered_set<int>& hoistedValues) {
  if (!operand.isSSA()) return true;
  if (operand.value_id < 0 || operand.value_id >= static_cast<int>(defBlock.size())) {
    return false;
  }
  int definingBlock = defBlock[operand.value_id];
  if (definingBlock < 0) return true;
  if (!loopContainsBlock(loop, definingBlock)) return true;
  return hoistedValues.count(operand.value_id) > 0;
}

bool isHoistableInstruction(const Instruction& inst) {
  if (!inst.has_result) return false;
  if (inst.kind == InstKind::Phi) return false;
  if (!isPureComputingInstruction(inst) || hasSideEffects(inst)) return false;
  if (inst.kind == InstKind::Load || inst.kind == InstKind::Call ||
      inst.kind == InstKind::Store) {
    return false;
  }
  return inst.kind == InstKind::Binary || inst.kind == InstKind::Unary ||
         inst.kind == InstKind::Copy;
}

std::vector<int> buildDefBlocks(const Function& function) {
  std::vector<int> defBlock(function.next_value_id, -1);
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size());
       ++blockIndex) {
    for (const auto& inst : function.blocks[blockIndex].instructions) {
      if (inst.has_result && inst.result_id >= 0 &&
          inst.result_id < static_cast<int>(defBlock.size())) {
        defBlock[inst.result_id] = blockIndex;
      }
    }
  }
  for (int param : function.params) {
    if (param >= 0 && param < static_cast<int>(defBlock.size()) &&
        function.entry_block >= 0) {
      defBlock[param] = function.entry_block;
    }
  }
  return defBlock;
}

bool hoistLoop(Function& function, const Loop& loop, const DominatorTree& domTree) {
  const int preheader = getLoopPreheader(function, loop);
  if (preheader < 0) return false;

  std::vector<int> defBlock = buildDefBlocks(function);
  std::unordered_set<int> hoistedValues;
  std::vector<Instruction> hoistedInstructions;
  bool changed = false;

  bool localChanged = true;
  while (localChanged) {
    localChanged = false;
    for (int blockIndex : loop.blocks) {
      auto& block = function.blocks[blockIndex];
      for (int instIndex = 0; instIndex < static_cast<int>(block.instructions.size());
           ++instIndex) {
        const auto& inst = block.instructions[instIndex];
        if (!isHoistableInstruction(inst)) continue;
        if (inst.result_id < 0 || hoistedValues.count(inst.result_id) > 0) continue;
        if (!std::all_of(inst.operands.begin(), inst.operands.end(),
                         [&](const ValueRef& operand) {
                           return isLoopInvariantOperand(operand, loop, defBlock,
                                                         hoistedValues);
                         })) {
          continue;
        }
        if (!dominatesAllLoopUses(function, loop, domTree, inst.result_id, preheader)) {
          continue;
        }

        hoistedValues.insert(inst.result_id);
        hoistedInstructions.push_back(inst);
        block.instructions.erase(block.instructions.begin() + instIndex);
        --instIndex;
        localChanged = true;
        changed = true;
      }
    }
  }

  if (!changed) return false;

  auto& preheaderBlock = function.blocks[preheader];
  int terminatorIndex = static_cast<int>(preheaderBlock.instructions.size()) - 1;
  if (terminatorIndex < 0 || !isTerminator(preheaderBlock.instructions.back().kind)) {
    return false;
  }
  preheaderBlock.instructions.insert(preheaderBlock.instructions.begin() + terminatorIndex,
                                     hoistedInstructions.begin(),
                                     hoistedInstructions.end());
  return true;
}

}  // namespace

std::string LICMPass::name() const { return "licm"; }

PassResult LICMPass::run(Function& function, AnalysisManager& analysisManager) {
  rebuildEdges(function);
  const DominatorTree& domTree = analysisManager.getDominatorTree(function);
  const LoopInfo& loopInfo = analysisManager.getLoopInfo(function);

  std::vector<int> order(loopInfo.loops.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
    return loopInfo.loops[lhs].depth > loopInfo.loops[rhs].depth;
  });

  bool changed = false;
  for (int loopIndex : order) {
    changed = hoistLoop(function, loopInfo.loops[loopIndex], domTree) || changed;
  }

  if (changed) {
    rebuildEdges(function);
    normalizePhiIncomings(function);
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

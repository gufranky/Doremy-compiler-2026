#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace midir {

namespace {

bool isLoopInvariantOperand(const ValueRef& operand, const Loop& loop,
                            const std::vector<int>& defBlock,
                            const std::unordered_set<int>& hoistedValues);

struct UseSite {
  int block_index = -1;
  bool is_phi_incoming = false;
  int pred_block = -1;
};

using UseLists = std::vector<std::vector<UseSite>>;

struct LoopMemorySummary {
  bool has_call = false;
  bool has_unknown_store = false;
  std::vector<MemoryLocation> store_locations;
};

bool loopContainsBlock(const Loop& loop, int block) {
  return std::binary_search(loop.blocks.begin(), loop.blocks.end(), block);
}

bool sameValueRef(const ValueRef& lhs, const ValueRef& rhs) {
  return lhs.kind == rhs.kind && lhs.type == rhs.type && lhs.value_id == rhs.value_id &&
         lhs.int_value == rhs.int_value && lhs.float_value == rhs.float_value &&
         lhs.frame_offset == rhs.frame_offset && lhs.symbol == rhs.symbol;
}

bool loopHasMemoryBarrier(const Function& function, const Loop& loop) {
  for (int blockIndex : loop.blocks) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& inst : block.instructions) {
      if (inst.kind == InstKind::Store || inst.kind == InstKind::Call) {
        return true;
      }
    }
  }
  return false;
}

UseLists buildUseLists(const Function& function) {
  UseLists uses(function.next_value_id);
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size()); ++blockIndex) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& inst : block.instructions) {
      for (const auto& operand : inst.operands) {
        if (!operand.isSSA() || operand.value_id < 0 ||
            operand.value_id >= static_cast<int>(uses.size())) {
          continue;
        }
        uses[operand.value_id].push_back(UseSite{blockIndex, false, -1});
      }
      if (inst.kind != InstKind::Phi) continue;
      for (const auto& incoming : inst.incomings) {
        if (!incoming.value.isSSA() || incoming.value.value_id < 0 ||
            incoming.value.value_id >= static_cast<int>(uses.size())) {
          continue;
        }
        uses[incoming.value.value_id].push_back(
            UseSite{blockIndex, true, incoming.pred_block});
      }
    }
  }
  return uses;
}

LoopMemorySummary buildLoopMemorySummary(
    const Function& function, const Loop& loop,
    const std::vector<int>& defBlock,
    const std::unordered_map<int, Instruction>& defs,
    MemoryAnalysisCache& memoryCache) {
  LoopMemorySummary summary;
  for (int blockIndex : loop.blocks) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& inst : block.instructions) {
      if (inst.kind == InstKind::Call) {
        summary.has_call = true;
        continue;
      }
      if (inst.kind != InstKind::Store) continue;
      if (inst.operands.size() != 2) {
        summary.has_unknown_store = true;
        continue;
      }
      MemoryLocation storeLocation =
          analyzeMemoryLocation(function, inst.operands[1], defBlock, defs, &memoryCache);
      storeLocation.access_size = typeStoreSize(inst.operands[0].type);
      if (!storeLocation.isIdentifiable() || storeLocation.access_size <= 0) {
        summary.has_unknown_store = true;
        continue;
      }
      summary.store_locations.push_back(storeLocation);
    }
  }
  return summary;
}

bool forwardInvariantLoadsInLoop(Function& function, const Loop& loop,
                                 const std::vector<int>& defBlock,
                                 const std::unordered_map<int, Instruction>& defs,
                                 MemoryAnalysisCache& memoryCache) {
  if (loopHasMemoryBarrier(function, loop)) return false;

  std::unordered_map<std::string, ValueRef> availableLoads;
  bool changed = false;
  const std::unordered_set<int> noHoistedValues;
  for (int blockIndex : loop.blocks) {
    auto& block = function.blocks[blockIndex];
    for (auto it = block.instructions.begin(); it != block.instructions.end();) {
      Instruction& inst = *it;
      if (inst.kind != InstKind::Load || !inst.has_result || inst.result_id < 0 ||
          inst.operands.size() != 1) {
        ++it;
        continue;
      }
      const ValueRef& addr = inst.operands[0];
      if (!isLoopInvariantOperand(addr, loop, defBlock, noHoistedValues)) {
        ++it;
        continue;
      }
      MemoryLocation loadLocation =
          analyzeMemoryLocation(function, addr, defBlock, defs, &memoryCache);
      loadLocation.access_size = typeStoreSize(inst.result_type);
      const std::string key = memoryLocationKey(loadLocation);
      if (key.empty()) {
        ++it;
        continue;
      }

      auto available = availableLoads.find(key);
      if (available == availableLoads.end()) {
        availableLoads.emplace(key, ValueRef::SSA(inst.result_id, inst.result_type));
        ++it;
        continue;
      }
      if (sameValueRef(available->second, ValueRef::SSA(inst.result_id, inst.result_type))) {
        ++it;
        continue;
      }

      replaceAllUses(function, inst.result_id, available->second);
      it = block.instructions.erase(it);
      changed = true;
    }
  }
  return changed;
}

bool dominatesAllLoopUses(const Function& function, const Loop& loop,
                          const DominatorTree& domTree,
                          const UseLists& useLists, int valueId,
                          int hoistBlock) {
  if (valueId < 0 || valueId >= static_cast<int>(useLists.size())) return false;
  for (const auto& use : useLists[valueId]) {
    if (use.is_phi_incoming) {
      if (!loopContainsBlock(loop, use.pred_block)) continue;
      if (use.pred_block != hoistBlock &&
          !dominates(domTree, hoistBlock, use.pred_block)) {
        return false;
      }
      continue;
    }
    if (!loopContainsBlock(loop, use.block_index)) continue;
    if (use.block_index != hoistBlock &&
        !dominates(domTree, hoistBlock, use.block_index)) {
      return false;
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
  if (inst.kind == InstKind::Call || inst.kind == InstKind::Store) return false;
  return inst.kind == InstKind::Binary || inst.kind == InstKind::Unary ||
         inst.kind == InstKind::Copy || inst.kind == InstKind::Load;
}

bool isInvariantLoadSafe(const Function& function, const Loop& loop,
                         const std::vector<int>& defBlock,
                         const std::unordered_set<int>& hoistedValues,
                         const std::unordered_map<int, Instruction>& defs,
                         const LoopMemorySummary& memorySummary,
                         MemoryAnalysisCache& memoryCache,
                         const Instruction& inst) {
  if (inst.kind != InstKind::Load || inst.operands.size() != 1) return false;
  const ValueRef& addr = inst.operands[0];
  if (!isLoopInvariantOperand(addr, loop, defBlock, hoistedValues)) return false;

  MemoryLocation load_location =
      analyzeMemoryLocation(function, addr, defBlock, defs, &memoryCache);
  if (!load_location.isIdentifiable()) return false;
  load_location.access_size = typeStoreSize(inst.result_type);
  if (load_location.access_size <= 0) return false;
  if (memorySummary.has_call || memorySummary.has_unknown_store) return false;

  for (const auto& store_location : memorySummary.store_locations) {
    if (!load_location.sameBase(store_location)) continue;
    if (rangesOverlap(load_location, store_location)) return false;
  }
  return true;
}

bool hoistLoop(Function& function, const Loop& loop, const DominatorTree& domTree,
               const UseLists& useLists) {
  const int preheader = getLoopPreheader(function, loop);
  if (preheader < 0) return false;

  std::vector<int> defBlock = buildDefBlocks(function);
  std::unordered_map<int, Instruction> defs = buildInstructionDefs(function);
  MemoryAnalysisCache memoryCache(function.next_value_id);
  const LoopMemorySummary memorySummary =
      buildLoopMemorySummary(function, loop, defBlock, defs, memoryCache);
  std::unordered_set<int> hoistedValues;
  std::vector<Instruction> hoistedInstructions;
  bool changed = false;
  changed = forwardInvariantLoadsInLoop(function, loop, defBlock, defs, memoryCache) || changed;

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
        if (inst.kind == InstKind::Load &&
            !isInvariantLoadSafe(function, loop, defBlock, hoistedValues, defs,
                                 memorySummary, memoryCache, inst)) {
          continue;
        }
        if (!dominatesAllLoopUses(function, loop, domTree, useLists,
                                  inst.result_id, preheader)) {
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
  const UseLists useLists = buildUseLists(function);

  std::vector<int> order(loopInfo.loops.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
    return loopInfo.loops[lhs].depth > loopInfo.loops[rhs].depth;
  });

  bool changed = false;
  for (int loopIndex : order) {
    changed = hoistLoop(function, loopInfo.loops[loopIndex], domTree, useLists) || changed;
  }

  if (changed) {
    rebuildEdges(function);
    normalizePhiIncomings(function);
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

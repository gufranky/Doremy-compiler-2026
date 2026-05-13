#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace midir {

namespace {

struct AvailableLoad {
  std::string key;
  ValueRef value = ValueRef::Invalid();
  MemoryLocation location;
  int alias_version = 0;
};

struct AvailableStore {
  std::string key;
  ValueRef value = ValueRef::Invalid();
  MemoryLocation location;
  int alias_version = 0;
};

struct GVNState {
  std::unordered_map<std::string, ValueRef> available_values;
  std::unordered_map<std::string, AvailableLoad> available_loads;
  std::unordered_map<std::string, AvailableStore> exact_stores;
  std::unordered_map<std::string, int> alias_versions;
};

void clearMemoryState(GVNState& state) {
  state.available_loads.clear();
  state.exact_stores.clear();
  state.alias_versions.clear();
}

bool sameValueRef(const ValueRef& lhs, const ValueRef& rhs) {
  return lhs.kind == rhs.kind && lhs.type == rhs.type && lhs.value_id == rhs.value_id &&
         lhs.int_value == rhs.int_value && lhs.float_value == rhs.float_value &&
         lhs.frame_offset == rhs.frame_offset && lhs.symbol == rhs.symbol;
}

std::string canonicalValueKey(const ValueRef& value) {
  return valueIdentityKey(value);
}

ValueRef getLeader(const std::vector<ValueRef>& leaders, ValueRef value) {
  while (value.isSSA() && value.value_id >= 0 &&
         value.value_id < static_cast<int>(leaders.size()) &&
         leaders[value.value_id].isValid()) {
    const ValueRef next = leaders[value.value_id];
    if (next.type != value.type) break;
    if (sameValueRef(next, value)) break;
    value = next;
  }
  return value;
}

void setLeader(std::vector<ValueRef>& leaders, int valueId, ValueRef leader,
               Type expectedType) {
  if (valueId < 0 || valueId >= static_cast<int>(leaders.size())) return;
  if (!leader.isValid() || leader.type != expectedType) return;
  leaders[valueId] = leader;
}

bool rewriteValueWithLeader(ValueRef& value, const std::vector<ValueRef>& leaders) {
  ValueRef leader = getLeader(leaders, value);
  if (!leader.isValid() || sameValueRef(leader, value)) return false;
  value = leader;
  return true;
}

bool rewriteInstructionOperandsWithLeaders(Instruction& inst,
                                          const std::vector<ValueRef>& leaders) {
  bool changed = false;
  for (auto& operand : inst.operands) {
    changed |= rewriteValueWithLeader(operand, leaders);
  }
  for (auto& incoming : inst.incomings) {
    changed |= rewriteValueWithLeader(incoming.value, leaders);
  }
  return changed;
}

std::string instructionKey(const Instruction& inst,
                           const std::vector<ValueRef>& leaders) {
  if (inst.kind == InstKind::Binary && inst.operands.size() == 2) {
    ValueRef lhs = getLeader(leaders, inst.operands[0]);
    ValueRef rhs = getLeader(leaders, inst.operands[1]);
    if (isCommutativeBinaryOp(inst.binary_op) &&
        canonicalValueKey(rhs) < canonicalValueKey(lhs)) {
      std::swap(lhs, rhs);
    }
    return "b:" + std::to_string(static_cast<int>(inst.binary_op)) + ":" +
           std::to_string(static_cast<int>(inst.operand_type.kind)) + ":" +
           std::to_string(static_cast<int>(inst.result_type.kind)) + ":" +
           canonicalValueKey(lhs) + ":" + canonicalValueKey(rhs);
  }
  if (inst.kind == InstKind::Unary && inst.operands.size() == 1) {
    return "u:" + std::to_string(static_cast<int>(inst.unary_op)) + ":" +
           std::to_string(static_cast<int>(inst.operand_type.kind)) + ":" +
           std::to_string(static_cast<int>(inst.result_type.kind)) + ":" +
           canonicalValueKey(getLeader(leaders, inst.operands[0]));
  }
  if (inst.kind == InstKind::Copy && inst.operands.size() == 1) {
    return "c:" + std::to_string(static_cast<int>(inst.result_type.kind)) + ":" +
           canonicalValueKey(getLeader(leaders, inst.operands[0]));
  }
  return {};
}

std::string memoryAccessKeyWithLeaders(const ValueRef& addr,
                                       const MemoryLocation& location,
                                       const std::vector<ValueRef>& leaders,
                                       Type accessType) {
  if (!isTrackableLocation(location)) return {};
  std::string key = memoryLocationKey(location);
  key += ":ty=" + std::to_string(static_cast<int>(accessType.kind));
  if (!location.offset_known) {
    key += ":addr=" + canonicalValueKey(getLeader(leaders, addr));
  }
  return key;
}

ValueRef findAvailableValueForLoad(const GVNState& state, const std::string& loadKey,
                                  const MemoryLocation& loadLocation,
                                  const std::vector<ValueRef>& leaders) {
  const int aliasVersion = currentAliasVersion(state.alias_versions, loadLocation);
  auto exactStore = state.exact_stores.find(loadKey);
  if (exactStore != state.exact_stores.end() &&
      exactStore->second.alias_version == aliasVersion) {
    return getLeader(leaders, exactStore->second.value);
  }
  auto existing = state.available_loads.find(loadKey);
  if (existing != state.available_loads.end() &&
      existing->second.alias_version == aliasVersion) {
    return getLeader(leaders, existing->second.value);
  }
  return ValueRef::Invalid();
}

int insertPhiAtTop(Function& function, int blockIndex, Type valueType) {
  if (blockIndex < 0 || blockIndex >= static_cast<int>(function.blocks.size())) {
    return -1;
  }

  Instruction phi;
  phi.kind = InstKind::Phi;
  phi.result_type = valueType;
  phi.result_id = function.newValue(valueType);
  phi.has_result = true;

  auto& block = function.blocks[blockIndex];
  int insertIndex = 0;
  while (insertIndex < static_cast<int>(block.instructions.size()) &&
         block.instructions[insertIndex].kind == InstKind::Phi) {
    ++insertIndex;
  }
  block.instructions.insert(block.instructions.begin() + insertIndex, std::move(phi));
  return block.instructions[insertIndex].result_id;
}

bool foldTrivialPhi(Instruction& inst, std::vector<ValueRef>& leaders);

bool tryEliminateJoinLoad(Function& function, int blockIndex, int instIndex,
                          const std::vector<GVNState>& exitStates,
                          std::vector<ValueRef>& leaders,
                          const std::vector<int>& defBlock,
                          std::unordered_map<int, Instruction>& defs,
                          MemoryAnalysisCache& memoryCache) {
  if (blockIndex < 0 || blockIndex >= static_cast<int>(function.blocks.size())) {
    return false;
  }

  auto& block = function.blocks[blockIndex];
  if (instIndex < 0 || instIndex >= static_cast<int>(block.instructions.size())) {
    return false;
  }

  const Instruction original = block.instructions[instIndex];
  if (original.kind != InstKind::Load || !original.has_result || original.result_id < 0 ||
      original.operands.size() != 1 || block.preds.size() < 2) {
    return false;
  }

  const int originalResultId = original.result_id;
  const Type originalResultType = original.result_type;

  MemoryLocation loadLocation =
      analyzeMemoryLocation(function, original.operands[0], defBlock, defs, &memoryCache);
  loadLocation.access_size = typeStoreSize(original.result_type);
  const std::string loadKey =
      memoryAccessKeyWithLeaders(original.operands[0], loadLocation, leaders, original.result_type);
  if (!isTrackableLocation(loadLocation) || loadKey.empty()) {
    return false;
  }

  std::vector<ValueRef> incomingValues;
  incomingValues.reserve(block.preds.size());
  ValueRef common = ValueRef::Invalid();
  bool allSame = true;
  for (int pred : block.preds) {
    if (pred < 0 || pred >= static_cast<int>(exitStates.size())) {
      return false;
    }
    const ValueRef available =
        findAvailableValueForLoad(exitStates[pred], loadKey, loadLocation, leaders);
    if (!available.isValid()) {
      return false;
    }
    incomingValues.push_back(available);
    if (!common.isValid()) {
      common = available;
      continue;
    }
    if (!sameValueRef(common, available)) {
      allSame = false;
    }
  }

  if (incomingValues.empty()) return false;

  ValueRef replacement = common;
  if (!allSame) {
    const int phiId = insertPhiAtTop(function, blockIndex, originalResultType);
    if (phiId < 0) return false;
    leaders.resize(function.next_value_id, ValueRef::Invalid());

    auto& updatedBlock = function.blocks[blockIndex];
    int phiInsertIndex = 0;
    while (phiInsertIndex < static_cast<int>(updatedBlock.instructions.size()) &&
           updatedBlock.instructions[phiInsertIndex].kind == InstKind::Phi) {
      if (updatedBlock.instructions[phiInsertIndex].result_id == phiId) {
        break;
      }
      ++phiInsertIndex;
    }
    if (phiInsertIndex >= static_cast<int>(updatedBlock.instructions.size()) ||
        updatedBlock.instructions[phiInsertIndex].kind != InstKind::Phi ||
        updatedBlock.instructions[phiInsertIndex].result_id != phiId) {
      return false;
    }

    updatedBlock.instructions[phiInsertIndex].incomings.clear();
    for (size_t i = 0; i < updatedBlock.preds.size(); ++i) {
      updatedBlock.instructions[phiInsertIndex].incomings.push_back(
          PhiIncoming{updatedBlock.preds[i], incomingValues[i]});
    }
    defs[phiId] = updatedBlock.instructions[phiInsertIndex];
    replacement = ValueRef::SSA(phiId, originalResultType);
  }

  const ValueRef self = ValueRef::SSA(originalResultId, originalResultType);
  if (sameValueRef(replacement, self)) return false;

  auto& finalBlock = function.blocks[blockIndex];
  const int finalInstIndex = allSame ? instIndex : instIndex + 1;
  if (finalInstIndex < 0 || finalInstIndex >= static_cast<int>(finalBlock.instructions.size())) {
    return false;
  }
  finalBlock.instructions[finalInstIndex] =
      makeCopyInstruction(finalBlock.instructions[finalInstIndex], replacement);
  setLeader(leaders, originalResultId, replacement, originalResultType);
  defs[originalResultId] = finalBlock.instructions[finalInstIndex];
  return true;
}

bool eliminateJoinLoads(Function& function, const std::vector<GVNState>& exitStates,
                        std::vector<ValueRef>& leaders,
                        const std::vector<int>& defBlock,
                        std::unordered_map<int, Instruction>& defs,
                        MemoryAnalysisCache& memoryCache) {
  bool changed = false;

  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size()); ++blockIndex) {
    auto& block = function.blocks[blockIndex];
    for (int instIndex = 0; instIndex < static_cast<int>(block.instructions.size()); ++instIndex) {
      auto& inst = block.instructions[instIndex];
      rewriteInstructionOperandsWithLeaders(inst, leaders);
      if (inst.kind == InstKind::Phi) {
        changed = foldTrivialPhi(inst, leaders) || changed;
        continue;
      }
      if (inst.kind == InstKind::Store || inst.kind == InstKind::Call) {
        break;
      }
      if (inst.kind != InstKind::Load) {
        continue;
      }
      if (tryEliminateJoinLoad(function, blockIndex, instIndex, exitStates, leaders,
                               defBlock, defs, memoryCache)) {
        changed = true;
        block = function.blocks[blockIndex];
        ++instIndex;
      }
    }
  }

  return changed;
}

bool foldTrivialPhi(Instruction& inst, std::vector<ValueRef>& leaders) {
  if (inst.kind != InstKind::Phi || !inst.has_result || inst.result_id < 0 ||
      inst.incomings.empty()) {
    return false;
  }

  const ValueRef self = ValueRef::SSA(inst.result_id, inst.result_type);
  ValueRef common = ValueRef::Invalid();
  for (auto& incoming : inst.incomings) {
    rewriteValueWithLeader(incoming.value, leaders);
    const ValueRef leader = getLeader(leaders, incoming.value);
    if (sameValueRef(leader, self)) {
      continue;
    }
    if (!common.isValid()) {
      common = leader;
      continue;
    }
    if (!sameValueRef(common, leader)) return false;
  }

  if (!common.isValid()) return false;
  if (sameValueRef(common, self)) return false;
  setLeader(leaders, inst.result_id, common, inst.result_type);
  return true;
}

bool processCopyInstruction(Instruction& inst, std::vector<ValueRef>& leaders) {
  if (inst.kind != InstKind::Copy || !inst.has_result || inst.result_id < 0 ||
      inst.operands.size() != 1) {
    return false;
  }

  const ValueRef leader = getLeader(leaders, inst.operands[0]);
  const bool changed = !sameValueRef(leader, inst.operands[0]);
  if (changed) {
    inst = makeCopyInstruction(inst, leader);
  }
  setLeader(leaders, inst.result_id, leader, inst.result_type);
  return changed;
}

bool processPureInstruction(Instruction& inst, GVNState& state,
                            std::vector<ValueRef>& leaders) {
  if (!inst.has_result || inst.result_id < 0) return false;
  if (inst.kind != InstKind::Binary && inst.kind != InstKind::Unary) return false;

  const std::string key = instructionKey(inst, leaders);
  if (key.empty()) return false;

  auto existing = state.available_values.find(key);
  if (existing != state.available_values.end()) {
    const ValueRef replacement = getLeader(leaders, existing->second);
    const ValueRef self = ValueRef::SSA(inst.result_id, inst.result_type);
    if (!sameValueRef(replacement, self)) {
      inst = makeCopyInstruction(inst, replacement);
      setLeader(leaders, inst.result_id, replacement, inst.result_type);
      return true;
    }
  }

  state.available_values[key] = ValueRef::SSA(inst.result_id, inst.result_type);
  return false;
}

bool processLoadInstruction(Function& function, Instruction& inst, GVNState& state,
                            std::vector<ValueRef>& leaders,
                            const std::vector<int>& defBlock,
                            std::unordered_map<int, Instruction>& defs,
                            MemoryAnalysisCache& memoryCache) {
  if (inst.kind != InstKind::Load || !inst.has_result || inst.result_id < 0 ||
      inst.operands.size() != 1) {
    return false;
  }

  MemoryLocation loadLocation =
      analyzeMemoryLocation(function, inst.operands[0], defBlock, defs, &memoryCache);
  loadLocation.access_size = typeStoreSize(inst.result_type);
  const std::string loadKey =
      memoryAccessKeyWithLeaders(inst.operands[0], loadLocation, leaders, inst.result_type);
  if (!isTrackableLocation(loadLocation) || loadKey.empty()) {
    return false;
  }

  const int aliasVersion = currentAliasVersion(state.alias_versions, loadLocation);
  ValueRef replacement = ValueRef::Invalid();
  auto exactStore = state.exact_stores.find(loadKey);
  if (exactStore != state.exact_stores.end() &&
      exactStore->second.alias_version == aliasVersion) {
    replacement = getLeader(leaders, exactStore->second.value);
  } else {
    auto existing = state.available_loads.find(loadKey);
    if (existing != state.available_loads.end() &&
        existing->second.alias_version == aliasVersion) {
      replacement = getLeader(leaders, existing->second.value);
    }
  }

  bool changed = false;
  const ValueRef self = ValueRef::SSA(inst.result_id, inst.result_type);
  if (replacement.isValid() && !sameValueRef(replacement, self)) {
    inst = makeCopyInstruction(inst, replacement);
    setLeader(leaders, inst.result_id, replacement, inst.result_type);
    changed = true;
  }

  state.available_loads[loadKey] =
      AvailableLoad{loadKey, ValueRef::SSA(inst.result_id, inst.result_type),
                    loadLocation, aliasVersion};
  return changed;
}

void processStoreInstruction(Function& function, const Instruction& inst, GVNState& state,
                             const std::vector<ValueRef>& leaders,
                             const std::vector<int>& defBlock,
                             const std::unordered_map<int, Instruction>& defs,
                             MemoryAnalysisCache& memoryCache) {
  if (inst.kind != InstKind::Store || inst.operands.size() != 2) {
    clearMemoryState(state);
    return;
  }

  MemoryLocation storeLocation =
      analyzeMemoryLocation(function, inst.operands[1], defBlock, defs, &memoryCache);
  storeLocation.access_size = typeStoreSize(inst.operands[0].type);
  const std::string storeKey =
      memoryAccessKeyWithLeaders(inst.operands[1], storeLocation, leaders, inst.operands[0].type);
  if (!isTrackableLocation(storeLocation) || storeKey.empty()) {
    clearMemoryState(state);
    return;
  }

  bumpAliasVersions(state.alias_versions, storeLocation);
  state.available_loads.erase(storeKey);
  state.exact_stores[storeKey] =
      AvailableStore{storeKey, getLeader(leaders, inst.operands[0]), storeLocation,
                     currentAliasVersion(state.alias_versions, storeLocation)};
}

GVNState entryStateForChild(const Function& function, int parentBlock, int childBlock,
                            const GVNState& parentExitState) {
  GVNState childState = parentExitState;
  if (childBlock < 0 || childBlock >= static_cast<int>(function.blocks.size())) {
    return childState;
  }
  const auto& block = function.blocks[childBlock];
  if (block.preds.size() != 1 || block.preds.front() != parentBlock) {
    clearMemoryState(childState);
  }
  return childState;
}

}  // namespace

std::string GVNPass::name() const { return "gvn"; }

PassResult GVNPass::run(Function& function,
                        AnalysisManager& analysisManager) {
  rebuildEdges(function);

  bool changed = false;
  const DominatorTree& domTree = analysisManager.getDominatorTree(function);
  const std::vector<int> defBlock = buildDefBlocks(function);
  std::unordered_map<int, Instruction> defs = buildInstructionDefs(function);
  MemoryAnalysisCache memoryCache(function.next_value_id);
  std::vector<ValueRef> leaders(function.next_value_id, ValueRef::Invalid());
  std::vector<GVNState> exitStates(function.blocks.size());

  std::function<void(int, GVNState)> processBlock = [&](int blockIndex, GVNState state) {
    auto& block = function.blocks[blockIndex];

    for (auto& inst : block.instructions) {
      bool instChanged = rewriteInstructionOperandsWithLeaders(inst, leaders);
      if (inst.kind == InstKind::Phi) {
        instChanged = foldTrivialPhi(inst, leaders) || instChanged;
      } else if (inst.kind == InstKind::Copy) {
        instChanged = processCopyInstruction(inst, leaders) || instChanged;
      } else if (inst.kind == InstKind::Binary || inst.kind == InstKind::Unary) {
        instChanged = processPureInstruction(inst, state, leaders) || instChanged;
      } else if (inst.kind == InstKind::Load) {
        instChanged = processLoadInstruction(function, inst, state, leaders, defBlock,
                                             defs, memoryCache) || instChanged;
      } else if (inst.kind == InstKind::Store) {
        processStoreInstruction(function, inst, state, leaders, defBlock, defs,
                                memoryCache);
      } else if (inst.kind == InstKind::Call) {
        clearMemoryState(state);
      }

      if (instChanged) {
        changed = true;
      }
      if (inst.has_result && inst.result_id >= 0) {
        defs[inst.result_id] = inst;
      }
    }

    exitStates[blockIndex] = state;
    for (int child : domTree.children[blockIndex]) {
      processBlock(child, entryStateForChild(function, blockIndex, child, state));
    }
  };

  if (function.entry_block >= 0 &&
      function.entry_block < static_cast<int>(function.blocks.size())) {
    processBlock(function.entry_block, GVNState{});
  }

  if (eliminateJoinLoads(function, exitStates, leaders, defBlock, defs, memoryCache)) {
    changed = true;
  }

  if (changed) {
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

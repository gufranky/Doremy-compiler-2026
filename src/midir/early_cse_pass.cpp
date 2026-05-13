#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
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
  int instruction_index = -1;
  int alias_version = 0;
};

std::string instructionKey(const Instruction& inst) {
  if (inst.kind == InstKind::Binary && inst.operands.size() == 2) {
    ValueRef lhs = inst.operands[0];
    ValueRef rhs = inst.operands[1];
    if (isCommutativeBinaryOp(inst.binary_op) && valueIdentityKey(rhs) < valueIdentityKey(lhs)) {
      std::swap(lhs, rhs);
    }
    return "b:" + std::to_string(static_cast<int>(inst.binary_op)) + ":" +
           std::to_string(static_cast<int>(inst.operand_type.kind)) + ":" +
           valueIdentityKey(lhs) + ":" + valueIdentityKey(rhs);
  }
  if (inst.kind == InstKind::Unary && inst.operands.size() == 1) {
    return "u:" + std::to_string(static_cast<int>(inst.unary_op)) + ":" +
           std::to_string(static_cast<int>(inst.operand_type.kind)) + ":" +
           valueIdentityKey(inst.operands[0]);
  }
  if (inst.kind == InstKind::Copy && inst.operands.size() == 1) {
    return "c:" + std::to_string(static_cast<int>(inst.result_type.kind)) + ":" +
           valueIdentityKey(inst.operands[0]);
  }
  return {};
}

bool sameValueRef(const ValueRef& lhs, const ValueRef& rhs) {
  return lhs.kind == rhs.kind && lhs.type == rhs.type && lhs.value_id == rhs.value_id &&
         lhs.int_value == rhs.int_value && lhs.float_value == rhs.float_value &&
         lhs.frame_offset == rhs.frame_offset && lhs.symbol == rhs.symbol;
}

}  // namespace

std::string EarlyCSEPass::name() const { return "early-cse"; }

PassResult EarlyCSEPass::run(Function& function,
                             AnalysisManager& analysisManager) {
  bool changed = false;
  const std::vector<int> defBlock = buildDefBlocks(function);
  const std::unordered_map<int, Instruction> defs = buildInstructionDefs(function);
  MemoryAnalysisCache memoryCache(function.next_value_id);

  for (auto& block : function.blocks) {
    std::unordered_map<std::string, ValueRef> availableValues;
    std::unordered_map<std::string, AvailableLoad> availableLoads;
    std::unordered_map<std::string, AvailableStore> exactStores;
    std::unordered_map<std::string, int> aliasVersions;
    std::vector<char> deadInstructions(block.instructions.size(), 0);

    for (size_t instIndex = 0; instIndex < block.instructions.size(); ++instIndex) {
      auto& inst = block.instructions[instIndex];

      if (inst.kind == InstKind::Call) {
        availableLoads.clear();
        exactStores.clear();
        aliasVersions.clear();
        continue;
      }

      if (inst.kind == InstKind::Store) {
        if (inst.operands.size() != 2) {
          availableLoads.clear();
          exactStores.clear();
          aliasVersions.clear();
          continue;
        }

        MemoryLocation storeLocation =
            analyzeMemoryLocation(function, inst.operands[1], defBlock, defs, &memoryCache);
        storeLocation.access_size = typeStoreSize(inst.operands[0].type);
        const std::string storeKey =
            memoryAccessKey(inst.operands[1], storeLocation, inst.operands[0].type);
        if (!isTrackableLocation(storeLocation) || storeKey.empty()) {
          availableLoads.clear();
          exactStores.clear();
          aliasVersions.clear();
          continue;
        }

        const int nextAliasVersion = currentAliasVersion(aliasVersions, storeLocation) + 1;
        auto previous = exactStores.find(storeKey);
        if (previous != exactStores.end() && previous->second.location.offset_known &&
            storeLocation.offset_known && previous->second.alias_version == nextAliasVersion - 1) {
          const int deadIndex = previous->second.instruction_index;
          if (deadIndex >= 0 && deadIndex < static_cast<int>(deadInstructions.size())) {
            deadInstructions[deadIndex] = 1;
            changed = true;
          }
        }

        bumpAliasVersions(aliasVersions, storeLocation);
        exactStores[storeKey] =
            AvailableStore{storeKey, inst.operands[0], storeLocation,
                           static_cast<int>(instIndex),
                           currentAliasVersion(aliasVersions, storeLocation)};
        continue;
      }

      if (inst.kind == InstKind::Load && inst.has_result && inst.result_id >= 0 &&
          inst.operands.size() == 1) {
        MemoryLocation loadLocation =
            analyzeMemoryLocation(function, inst.operands[0], defBlock, defs, &memoryCache);
        loadLocation.access_size = typeStoreSize(inst.result_type);
        const std::string loadKey =
            memoryAccessKey(inst.operands[0], loadLocation, inst.result_type);
        if (!isTrackableLocation(loadLocation) || loadKey.empty()) {
          availableLoads.clear();
          exactStores.clear();
          aliasVersions.clear();
          continue;
        }

        const int aliasVersion = currentAliasVersion(aliasVersions, loadLocation);
        ValueRef replacement = ValueRef::Invalid();
        auto exactStore = exactStores.find(loadKey);
        if (exactStore != exactStores.end() && exactStore->second.alias_version == aliasVersion) {
          replacement = exactStore->second.value;
        } else {
          auto existing = availableLoads.find(loadKey);
          if (existing != availableLoads.end() && existing->second.alias_version == aliasVersion) {
            replacement = existing->second.value;
          }
        }

        if (replacement.isValid() &&
            !sameValueRef(replacement, ValueRef::SSA(inst.result_id, inst.result_type))) {
          inst = makeCopyInstruction(inst, replacement);
          changed = true;
        }

        availableLoads[loadKey] = AvailableLoad{loadKey,
                                                ValueRef::SSA(inst.result_id,
                                                              inst.result_type),
                                                loadLocation,
                                                aliasVersion};
        exactStores.erase(loadKey);
        continue;
      }

      if (inst.has_result && isPureComputingInstruction(inst) &&
          inst.kind != InstKind::Phi && inst.kind != InstKind::Load) {
        const std::string key = instructionKey(inst);
        if (key.empty()) continue;
        auto existing = availableValues.find(key);
        if (existing != availableValues.end()) {
          inst = makeCopyInstruction(inst, existing->second);
          changed = true;
          continue;
        }
        availableValues.emplace(key, ValueRef::SSA(inst.result_id, inst.result_type));
      }
    }

    if (std::any_of(deadInstructions.begin(), deadInstructions.end(),
                    [](char dead) { return dead != 0; })) {
      std::vector<Instruction> kept;
      kept.reserve(block.instructions.size());
      for (size_t i = 0; i < block.instructions.size(); ++i) {
        if (deadInstructions[i]) continue;
        kept.push_back(block.instructions[i]);
      }
      block.instructions = std::move(kept);
    }
  }

  if (changed) {
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

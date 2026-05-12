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

std::string valueKey(const ValueRef& value) {
  switch (value.kind) {
    case ValueRef::Kind::SSA:
      return "s" + std::to_string(value.value_id) + ":" +
             std::to_string(static_cast<int>(value.type.kind));
    case ValueRef::Kind::ImmediateInt:
      return "i" + std::to_string(value.int_value);
    case ValueRef::Kind::ImmediateFloat:
      return "f" + std::to_string(value.float_value);
    case ValueRef::Kind::GlobalSymbol:
      return "g" + value.symbol;
    case ValueRef::Kind::FrameAddress:
      return "fa" + std::to_string(value.frame_offset);
    case ValueRef::Kind::StackPointer:
      return "sp";
    case ValueRef::Kind::Undef:
      return "u" + std::to_string(static_cast<int>(value.type.kind));
    case ValueRef::Kind::Invalid:
      return "invalid";
  }
  return "invalid";
}

std::string instructionKey(const Instruction& inst) {
  if (inst.kind == InstKind::Binary && inst.operands.size() == 2) {
    ValueRef lhs = inst.operands[0];
    ValueRef rhs = inst.operands[1];
    if (isCommutativeBinaryOp(inst.binary_op) && valueKey(rhs) < valueKey(lhs)) {
      std::swap(lhs, rhs);
    }
    return "b:" + std::to_string(static_cast<int>(inst.binary_op)) + ":" +
           std::to_string(static_cast<int>(inst.operand_type.kind)) + ":" +
           valueKey(lhs) + ":" + valueKey(rhs);
  }
  if (inst.kind == InstKind::Unary && inst.operands.size() == 1) {
    return "u:" + std::to_string(static_cast<int>(inst.unary_op)) + ":" +
           std::to_string(static_cast<int>(inst.operand_type.kind)) + ":" +
           valueKey(inst.operands[0]);
  }
  if (inst.kind == InstKind::Copy && inst.operands.size() == 1) {
    return "c:" + std::to_string(static_cast<int>(inst.result_type.kind)) + ":" +
           valueKey(inst.operands[0]);
  }
  return {};
}

bool sameValueRef(const ValueRef& lhs, const ValueRef& rhs) {
  return lhs.kind == rhs.kind && lhs.type == rhs.type && lhs.value_id == rhs.value_id &&
         lhs.int_value == rhs.int_value && lhs.float_value == rhs.float_value &&
         lhs.frame_offset == rhs.frame_offset && lhs.symbol == rhs.symbol;
}

bool isTrackableLocation(const MemoryLocation& location) {
  return location.isIdentifiable() && location.access_size > 0;
}

std::string memoryAccessKey(const ValueRef& addr, const MemoryLocation& location,
                            Type accessType) {
  if (!isTrackableLocation(location)) return {};
  std::string key = memoryLocationKey(location);
  key += ":ty=" + std::to_string(static_cast<int>(accessType.kind));
  if (!location.offset_known) {
    key += ":addr=" + valueKey(addr);
  }
  return key;
}

std::string aliasClassKey(const MemoryLocation& location) {
  if (!isTrackableLocation(location)) return {};
  switch (location.base_kind) {
    case MemoryLocation::BaseKind::Global:
      return "g:" + location.symbol;
    case MemoryLocation::BaseKind::Frame:
      return "f:" + std::to_string(location.frame_offset);
    case MemoryLocation::BaseKind::Stack:
      return "sp";
    case MemoryLocation::BaseKind::Param:
      return "p:" + std::to_string(location.param_index);
    case MemoryLocation::BaseKind::Invalid:
    case MemoryLocation::BaseKind::Unknown:
      return {};
  }
  return {};
}

std::vector<std::string> affectedAliasClassKeys(const MemoryLocation& location) {
  std::vector<std::string> keys;
  if (!isTrackableLocation(location)) return keys;

  const std::string current = aliasClassKey(location);
  switch (location.base_kind) {
    case MemoryLocation::BaseKind::Global:
    case MemoryLocation::BaseKind::Frame:
      if (!current.empty()) keys.push_back(current);
      break;
    case MemoryLocation::BaseKind::Stack:
      keys.push_back("sp");
      break;
    case MemoryLocation::BaseKind::Param:
      keys.push_back("params");
      break;
    case MemoryLocation::BaseKind::Invalid:
    case MemoryLocation::BaseKind::Unknown:
      break;
  }
  return keys;
}

int currentAliasVersion(
    const std::unordered_map<std::string, int>& aliasVersions,
    const MemoryLocation& location) {
  if (!isTrackableLocation(location)) return 0;

  const auto readVersion = [&](const std::string& key) -> int {
    auto it = aliasVersions.find(key);
    return it == aliasVersions.end() ? 0 : it->second;
  };

  switch (location.base_kind) {
    case MemoryLocation::BaseKind::Global:
    case MemoryLocation::BaseKind::Frame:
      return readVersion(aliasClassKey(location));
    case MemoryLocation::BaseKind::Stack:
      return readVersion("sp");
    case MemoryLocation::BaseKind::Param: {
      const int exact = readVersion(aliasClassKey(location));
      const int broad = readVersion("params");
      return std::max(exact, broad);
    }
    case MemoryLocation::BaseKind::Invalid:
    case MemoryLocation::BaseKind::Unknown:
      return 0;
  }
  return 0;
}

void bumpAliasVersions(std::unordered_map<std::string, int>& aliasVersions,
                       const MemoryLocation& location) {
  for (const std::string& key : affectedAliasClassKeys(location)) {
    ++aliasVersions[key];
  }
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

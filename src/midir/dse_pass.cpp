#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace midir {

namespace {

struct TrackedStore {
  MemoryLocation location;
  int block_index = -1;
  int instruction_index = -1;
  int alias_version = 0;
};

bool isTrackableLocation(const MemoryLocation& location) {
  return location.isIdentifiable() && location.access_size > 0;
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

bool isLinearSuccessor(const Function& function, int blockIndex, int succBlock) {
  if (blockIndex < 0 || succBlock < 0 ||
      blockIndex >= static_cast<int>(function.blocks.size()) ||
      succBlock >= static_cast<int>(function.blocks.size())) {
    return false;
  }
  const auto& block = function.blocks[blockIndex];
  const auto& succ = function.blocks[succBlock];
  return block.succs.size() == 1 && block.succs.front() == succBlock &&
         succ.preds.size() == 1 && succ.preds.front() == blockIndex;
}

}  // namespace

std::string DSEPass::name() const { return "dse"; }

PassResult DSEPass::run(Function& function,
                        AnalysisManager& analysisManager) {
  rebuildEdges(function);

  bool changed = false;
  const std::vector<int> defBlock = buildDefBlocks(function);
  const std::unordered_map<int, Instruction> defs = buildInstructionDefs(function);
  MemoryAnalysisCache memoryCache(function.next_value_id);
  std::vector<std::vector<char>> deadInstructions(function.blocks.size());
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size()); ++blockIndex) {
    deadInstructions[blockIndex].assign(function.blocks[blockIndex].instructions.size(), 0);
  }
  std::vector<char> linearPredCount(function.blocks.size(), 0);
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size()); ++blockIndex) {
    for (int succBlock : function.blocks[blockIndex].succs) {
      if (isLinearSuccessor(function, blockIndex, succBlock)) {
        linearPredCount[succBlock] = 1;
      }
    }
  }

  for (int startBlock = 0; startBlock < static_cast<int>(function.blocks.size()); ++startBlock) {
    if (linearPredCount[startBlock]) continue;
    std::unordered_map<std::string, int> aliasVersions;
    std::unordered_map<std::string, TrackedStore> exactStores;

    int blockIndex = startBlock;
    while (blockIndex >= 0) {
      auto& block = function.blocks[blockIndex];

      for (int instIndex = 0; instIndex < static_cast<int>(block.instructions.size()); ++instIndex) {
        auto& inst = block.instructions[instIndex];

        if (inst.kind == InstKind::Call) {
          exactStores.clear();
          aliasVersions.clear();
          continue;
        }

        if (inst.kind == InstKind::Load) {
          exactStores.clear();
          aliasVersions.clear();
          continue;
        }

        if (inst.kind != InstKind::Store) continue;
        if (inst.operands.size() != 2) {
          exactStores.clear();
          aliasVersions.clear();
          continue;
        }

        MemoryLocation storeLocation =
            analyzeMemoryLocation(function, inst.operands[1], defBlock, defs, &memoryCache);
        storeLocation.access_size = typeStoreSize(inst.operands[0].type);
        if (!isTrackableLocation(storeLocation)) {
          exactStores.clear();
          aliasVersions.clear();
          continue;
        }

        const int nextAliasVersion = currentAliasVersion(aliasVersions, storeLocation) + 1;
        const std::string exactKey = memoryLocationKey(storeLocation);
        auto previous = exactStores.find(exactKey);
        if (!exactKey.empty() && previous != exactStores.end() &&
            previous->second.alias_version == nextAliasVersion - 1 &&
            previous->second.location.offset_known && storeLocation.offset_known &&
            previous->second.location.offset == storeLocation.offset &&
            previous->second.location.access_size == storeLocation.access_size) {
          const auto& tracked = previous->second;
          if (tracked.block_index >= 0 &&
              tracked.block_index < static_cast<int>(deadInstructions.size()) &&
              tracked.instruction_index >= 0 &&
              tracked.instruction_index <
                  static_cast<int>(deadInstructions[tracked.block_index].size())) {
            deadInstructions[tracked.block_index][tracked.instruction_index] = 1;
            changed = true;
          }
        }

        bumpAliasVersions(aliasVersions, storeLocation);
        if (!exactKey.empty()) {
          exactStores[exactKey] =
              TrackedStore{storeLocation, blockIndex, instIndex,
                           currentAliasVersion(aliasVersions, storeLocation)};
        }
      }

      if (block.succs.size() != 1) break;
      const int succBlock = block.succs.front();
      if (!isLinearSuccessor(function, blockIndex, succBlock)) break;
      blockIndex = succBlock;
    }
  }

  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size()); ++blockIndex) {
    auto& block = function.blocks[blockIndex];
    auto& dead = deadInstructions[blockIndex];
    if (!std::any_of(dead.begin(), dead.end(), [](char value) { return value != 0; })) {
      continue;
    }
    std::vector<Instruction> kept;
    kept.reserve(block.instructions.size());
    for (size_t i = 0; i < block.instructions.size(); ++i) {
      if (dead[i]) continue;
      kept.push_back(std::move(block.instructions[i]));
    }
    block.instructions = std::move(kept);
  }

  if (changed) {
    rebuildEdges(function);
    normalizePhiIncomings(function);
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

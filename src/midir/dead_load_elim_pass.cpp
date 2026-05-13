#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <unordered_map>
#include <vector>

namespace midir {

namespace {

struct AvailableLoad {
  ValueRef value = ValueRef::Invalid();
  MemoryLocation location;
  int instruction_index = -1;
  int alias_version = 0;
};

}  // namespace

std::string DeadLoadElimPass::name() const { return "dead-load-elim"; }

PassResult DeadLoadElimPass::run(Function& function,
                                 AnalysisManager& analysisManager) {
  bool changed = false;
  const std::vector<int> defBlock = buildDefBlocks(function);
  const std::unordered_map<int, Instruction> defs = buildInstructionDefs(function);
  MemoryAnalysisCache memoryCache(function.next_value_id);

  for (auto& block : function.blocks) {
    std::unordered_map<std::string, AvailableLoad> availableLoads;
    std::unordered_map<std::string, int> aliasVersions;
    std::vector<char> deadInstructions(block.instructions.size(), 0);

    for (int instIndex = 0; instIndex < static_cast<int>(block.instructions.size()); ++instIndex) {
      auto& inst = block.instructions[instIndex];

      if (inst.kind == InstKind::Call) {
        availableLoads.clear();
        aliasVersions.clear();
        continue;
      }

      if (inst.kind == InstKind::Store) {
        if (inst.operands.size() != 2) {
          availableLoads.clear();
          aliasVersions.clear();
          continue;
        }
        MemoryLocation storeLocation =
            analyzeMemoryLocation(function, inst.operands[1], defBlock, defs, &memoryCache);
        storeLocation.access_size = typeStoreSize(inst.operands[0].type);
        if (!isTrackableLocation(storeLocation)) {
          availableLoads.clear();
          aliasVersions.clear();
          continue;
        }
        bumpAliasVersions(aliasVersions, storeLocation);
        continue;
      }

      if (inst.kind != InstKind::Load || !inst.has_result || inst.result_id < 0 ||
          inst.operands.size() != 1) {
        continue;
      }

      MemoryLocation loadLocation =
          analyzeMemoryLocation(function, inst.operands[0], defBlock, defs, &memoryCache);
      loadLocation.access_size = typeStoreSize(inst.result_type);
      const std::string loadKey =
          memoryAccessKey(inst.operands[0], loadLocation, inst.result_type);
      if (!isTrackableLocation(loadLocation) || loadKey.empty()) {
        availableLoads.clear();
        aliasVersions.clear();
        continue;
      }

      const int aliasVersion = currentAliasVersion(aliasVersions, loadLocation);
      auto existing = availableLoads.find(loadKey);
      if (existing != availableLoads.end() && existing->second.alias_version == aliasVersion) {
        replaceAllUses(function, inst.result_id, existing->second.value);
        deadInstructions[instIndex] = 1;
        changed = true;
        continue;
      }

      availableLoads[loadKey] =
          AvailableLoad{ValueRef::SSA(inst.result_id, inst.result_type), loadLocation,
                        instIndex, aliasVersion};
    }

    if (!changed) continue;
    std::vector<Instruction> kept;
    kept.reserve(block.instructions.size());
    for (size_t i = 0; i < block.instructions.size(); ++i) {
      if (deadInstructions[i]) continue;
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

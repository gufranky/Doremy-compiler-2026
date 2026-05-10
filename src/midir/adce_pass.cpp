#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <vector>

namespace midir {

std::string ADCEPass::name() const { return "adce"; }

PassResult ADCEPass::run(Function& function,
                         AnalysisManager& analysisManager) {
  std::vector<bool> live(function.next_value_id, false);
  bool changed = false;

  for (const auto& block : function.blocks) {
    for (const auto& inst : block.instructions) {
      if (hasSideEffects(inst)) {
        for (const auto& operand : inst.operands) {
          if (operand.isSSA() && operand.value_id >= 0 &&
              operand.value_id < static_cast<int>(live.size())) {
            live[operand.value_id] = true;
          }
        }
        for (const auto& incoming : inst.incomings) {
          if (incoming.value.isSSA() && incoming.value.value_id >= 0 &&
              incoming.value.value_id < static_cast<int>(live.size())) {
            live[incoming.value.value_id] = true;
          }
        }
      }
    }
  }

  bool progress = true;
  while (progress) {
    progress = false;
    for (const auto& block : function.blocks) {
      for (const auto& inst : block.instructions) {
        if (!inst.has_result || inst.result_id < 0 ||
            inst.result_id >= static_cast<int>(live.size()) || !live[inst.result_id]) {
          continue;
        }
        for (const auto& operand : inst.operands) {
          if (operand.isSSA() && operand.value_id >= 0 &&
              operand.value_id < static_cast<int>(live.size()) &&
              !live[operand.value_id]) {
            live[operand.value_id] = true;
            progress = true;
          }
        }
        for (const auto& incoming : inst.incomings) {
          if (incoming.value.isSSA() && incoming.value.value_id >= 0 &&
              incoming.value.value_id < static_cast<int>(live.size()) &&
              !live[incoming.value.value_id]) {
            live[incoming.value.value_id] = true;
            progress = true;
          }
        }
      }
    }
  }

  for (auto& block : function.blocks) {
    std::vector<Instruction> kept;
    kept.reserve(block.instructions.size());
    for (auto& inst : block.instructions) {
      if (inst.has_result && inst.result_id >= 0 &&
          inst.result_id < static_cast<int>(live.size()) && !live[inst.result_id] &&
          !hasSideEffects(inst)) {
        changed = true;
        continue;
      }
      kept.push_back(std::move(inst));
    }
    block.instructions = std::move(kept);
  }

  if (changed) {
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

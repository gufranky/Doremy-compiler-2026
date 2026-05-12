#include "optimizer_pipeline.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "optimizer_utils.h"

namespace midir {

namespace {

constexpr int kAlwaysInlineInstLimit = 16;
constexpr int kAlwaysInlineBlockLimit = 3;
constexpr int kHotInlineInstLimit = 128;
constexpr int kHotInlineBlockLimit = 16;
constexpr int kHotInlineCallsiteLimit = 2;
constexpr int kSingleCallsiteInstLimit = 160;
constexpr int kSingleCallsiteBlockLimit = 40;

struct InlineCandidate {
  Function* callee = nullptr;
  int callsite_count = 0;
};

Instruction makeJumpInstruction(const std::string& target) {
  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = target;
  return jump;
}

Instruction makeReturnMergePhi(Function& function, Type type,
                               std::vector<PhiIncoming> incomings) {
  Instruction phi;
  phi.kind = InstKind::Phi;
  phi.result_type = type;
  phi.result_id = function.newValue(type);
  phi.has_result = true;
  phi.incomings = std::move(incomings);
  return phi;
}

bool hasFrameAddressOperand(const ValueRef& value) {
  return value.kind == ValueRef::Kind::FrameAddress;
}

bool hasFrameAddress(const Function& function) {
  for (const auto& block : function.blocks) {
    for (const auto& inst : block.instructions) {
      for (const auto& operand : inst.operands) {
        if (hasFrameAddressOperand(operand)) return true;
      }
      for (const auto& incoming : inst.incomings) {
        if (hasFrameAddressOperand(incoming.value)) return true;
      }
    }
  }
  return false;
}

bool isLeafFunction(const Function& function) {
  for (const auto& block : function.blocks) {
    for (const auto& inst : block.instructions) {
      if (inst.kind == InstKind::Call) return false;
    }
  }
  return true;
}

int countInterestingInstructions(const Function& function) {
  int count = 0;
  for (const auto& block : function.blocks) {
    for (const auto& inst : block.instructions) {
      if (inst.kind == InstKind::Phi || isTerminator(inst.kind)) continue;
      ++count;
    }
  }
  return count;
}

int countStaticCallsites(const Module& module, const std::string& calleeName) {
  int count = 0;
  for (const auto& function : module.functions) {
    for (const auto& block : function.blocks) {
      for (const auto& inst : block.instructions) {
        if (inst.kind == InstKind::Call && inst.callee == calleeName) {
          ++count;
        }
      }
    }
  }
  return count;
}

Function* findCallee(Module& module, const std::string& name) {
  for (auto& function : module.functions) {
    if (function.name == name) return &function;
  }
  return nullptr;
}

bool isLegalCallsite(const Function& caller, const Instruction& call, const Function& callee) {
  if (&caller == &callee) return false;
  if (call.kind != InstKind::Call) return false;
  if (!caller.in_ssa || !callee.in_ssa) return false;
  if (callee.local_array_size != 0) return false;
  if (hasFrameAddress(callee)) return false;
  if (!isLeafFunction(callee)) return false;
  if (call.operands.size() != callee.param_types.size()) return false;
  if (call.call_arg_types.size() != callee.param_types.size()) return false;
  if (call.has_result != (callee.return_type != Type::Void())) return false;
  if (call.has_result && call.result_type != callee.return_type) return false;
  for (size_t i = 0; i < call.operands.size(); ++i) {
    if (call.call_arg_types[i] != callee.param_types[i]) return false;
    if (call.operands[i].type != callee.param_types[i]) return false;
  }
  return true;
}

bool isProfitableInline(const Function& callee, int callsiteCount) {
  const int blockCount = static_cast<int>(callee.blocks.size());
  const int instCount = countInterestingInstructions(callee);
  if (instCount <= kAlwaysInlineInstLimit || blockCount <= kAlwaysInlineBlockLimit) {
    return true;
  }
  if (blockCount <= kHotInlineBlockLimit && instCount <= kHotInlineInstLimit &&
      callsiteCount <= kHotInlineCallsiteLimit) {
    return true;
  }
  return callsiteCount == 1 && blockCount <= kSingleCallsiteBlockLimit &&
         instCount <= kSingleCallsiteInstLimit;
}

std::optional<InlineCandidate> analyzeCandidate(Module& module, const Function& caller,
                                                const Instruction& call) {
  Function* callee = findCallee(module, call.callee);
  if (callee == nullptr) return std::nullopt;
  if (!isLegalCallsite(caller, call, *callee)) return std::nullopt;
  const int callsiteCount = countStaticCallsites(module, callee->name);
  if (!isProfitableInline(*callee, callsiteCount)) return std::nullopt;
  return InlineCandidate{callee, callsiteCount};
}

void remapValue(ValueRef& value, const std::unordered_map<int, ValueRef>& valueMap) {
  if (!value.isSSA()) return;
  auto it = valueMap.find(value.value_id);
  if (it == valueMap.end()) return;
  value = it->second;
}

void remapInstructionOperands(Instruction& inst,
                              const std::unordered_map<int, ValueRef>& valueMap) {
  for (auto& operand : inst.operands) {
    remapValue(operand, valueMap);
  }
  for (auto& incoming : inst.incomings) {
    remapValue(incoming.value, valueMap);
  }
}

std::string makeInlineBlockName(Function& function, const std::string& base, int salt) {
  std::string prefix = base + ".inl" + std::to_string(salt);
  std::string name = prefix;
  int suffix = 0;
  while (function.block_index_by_name.count(name) != 0) {
    name = prefix + "." + std::to_string(++suffix);
  }
  return name;
}

bool splitCallBlock(Function& caller, int blockIndex, int instIndex, int* contBlockIndex) {
  if (blockIndex < 0 || blockIndex >= static_cast<int>(caller.blocks.size())) return false;
  auto& block = caller.blocks[blockIndex];
  if (instIndex < 0 || instIndex >= static_cast<int>(block.instructions.size())) return false;

  BasicBlock cont;
  cont.name = makeInlineBlockName(caller, block.name + ".cont", instIndex);
  cont.instructions.assign(block.instructions.begin() + instIndex + 1, block.instructions.end());

  block.instructions.erase(block.instructions.begin() + instIndex, block.instructions.end());
  block.instructions.push_back(makeJumpInstruction(cont.name));

  *contBlockIndex = static_cast<int>(caller.blocks.size());
  caller.blocks.push_back(std::move(cont));
  caller.block_index_by_name[caller.blocks.back().name] = *contBlockIndex;
  return true;
}

bool inlineCallsite(Function& caller, Module& module, int blockIndex, int instIndex) {
  if (blockIndex < 0 || blockIndex >= static_cast<int>(caller.blocks.size())) return false;
  auto& callBlock = caller.blocks[blockIndex];
  if (instIndex < 0 || instIndex >= static_cast<int>(callBlock.instructions.size())) return false;
  const Instruction callInst = callBlock.instructions[instIndex];
  if (callInst.kind != InstKind::Call) return false;

  std::optional<InlineCandidate> candidate = analyzeCandidate(module, caller, callInst);
  if (!candidate.has_value()) return false;
  Function& callee = *candidate->callee;
  if (callee.entry_block < 0 || callee.entry_block >= static_cast<int>(callee.blocks.size())) {
    return false;
  }

  const int originalBlockCount = static_cast<int>(caller.blocks.size());
  int contBlockIndex = -1;
  if (!splitCallBlock(caller, blockIndex, instIndex, &contBlockIndex)) return false;

  std::unordered_map<int, int> blockMap;
  std::unordered_map<int, ValueRef> valueMap;
  blockMap.reserve(callee.blocks.size());
  valueMap.reserve(callee.next_value_id + callee.params.size());

  for (size_t i = 0; i < callee.params.size() && i < callInst.operands.size(); ++i) {
    valueMap[callee.params[i]] = callInst.operands[i];
  }
  for (const auto& oldBlock : callee.blocks) {
    for (const auto& oldInst : oldBlock.instructions) {
      if (!oldInst.has_result) continue;
      valueMap[oldInst.result_id] =
          ValueRef::SSA(caller.newValue(oldInst.result_type), oldInst.result_type);
    }
  }

  std::vector<int> clonedBlockIndices;
  clonedBlockIndices.reserve(callee.blocks.size());
  for (size_t oldIndex = 0; oldIndex < callee.blocks.size(); ++oldIndex) {
    BasicBlock cloned;
    cloned.name = makeInlineBlockName(caller, callee.blocks[oldIndex].name, blockIndex);
    const int newIndex = static_cast<int>(caller.blocks.size());
    blockMap[static_cast<int>(oldIndex)] = newIndex;
    clonedBlockIndices.push_back(newIndex);
    caller.blocks.push_back(std::move(cloned));
    caller.block_index_by_name[caller.blocks.back().name] = newIndex;
  }

  std::vector<PhiIncoming> returnIncomings;
  for (size_t oldIndex = 0; oldIndex < callee.blocks.size(); ++oldIndex) {
    const int newIndex = blockMap[static_cast<int>(oldIndex)];
    auto& clonedBlock = caller.blocks[newIndex];
    const auto& oldBlock = callee.blocks[oldIndex];

    for (const auto& oldInst : oldBlock.instructions) {
      if (oldInst.kind == InstKind::Return) {
        if (oldInst.has_value) {
          if (!callInst.has_result || oldInst.operands.size() != 1) return false;
          ValueRef returnValue = oldInst.operands[0];
          remapValue(returnValue, valueMap);
          if (returnValue.type != callInst.result_type) return false;
          returnIncomings.push_back(PhiIncoming{newIndex, returnValue});
        } else if (callInst.has_result) {
          return false;
        }
        clonedBlock.instructions.push_back(makeJumpInstruction(caller.blocks[contBlockIndex].name));
        continue;
      }

      Instruction clonedInst = oldInst;
      remapInstructionOperands(clonedInst, valueMap);
      if (clonedInst.kind == InstKind::Phi) {
        for (auto& incoming : clonedInst.incomings) {
          auto predIt = blockMap.find(incoming.pred_block);
          if (predIt == blockMap.end()) return false;
          incoming.pred_block = predIt->second;
        }
      }
      if (clonedInst.kind == InstKind::Branch) {
        auto trueIt = callee.block_index_by_name.find(clonedInst.true_target);
        auto falseIt = callee.block_index_by_name.find(clonedInst.false_target);
        if (trueIt == callee.block_index_by_name.end() ||
            falseIt == callee.block_index_by_name.end()) {
          return false;
        }
        clonedInst.true_target = caller.blocks[blockMap[trueIt->second]].name;
        clonedInst.false_target = caller.blocks[blockMap[falseIt->second]].name;
      } else if (clonedInst.kind == InstKind::Jump) {
        auto targetIt = callee.block_index_by_name.find(clonedInst.jump_target);
        if (targetIt == callee.block_index_by_name.end()) return false;
        clonedInst.jump_target = caller.blocks[blockMap[targetIt->second]].name;
      }
      if (clonedInst.has_result) {
        const int oldResult = oldInst.result_id;
        auto mappedIt = valueMap.find(oldResult);
        if (mappedIt == valueMap.end() || !mappedIt->second.isSSA()) return false;
        clonedInst.result_id = mappedIt->second.value_id;
        clonedInst.result_type = mappedIt->second.type;
        clonedInst.legacy_result_id = -1;
      }
      clonedBlock.instructions.push_back(std::move(clonedInst));
    }
  }

  if (callInst.has_result) {
    if (returnIncomings.empty()) return false;
    Instruction phi = makeReturnMergePhi(caller, callInst.result_type, std::move(returnIncomings));
    const ValueRef merged = ValueRef::SSA(phi.result_id, phi.result_type);
    auto& contBlock = caller.blocks[contBlockIndex];
    int insertIndex = 0;
    while (insertIndex < static_cast<int>(contBlock.instructions.size()) &&
           contBlock.instructions[insertIndex].kind == InstKind::Phi) {
      ++insertIndex;
    }
    contBlock.instructions.insert(contBlock.instructions.begin() + insertIndex, std::move(phi));
    replaceAllUses(caller, callInst.result_id, merged);
  }

  caller.blocks[blockIndex].instructions.back().jump_target =
      caller.blocks[blockMap[callee.entry_block]].name;

  rebuildEdges(caller);
  for (int succ : caller.blocks[contBlockIndex].succs) {
    rewritePhiForEdgeRedirect(caller, succ, blockIndex, {contBlockIndex});
  }
  rebuildEdges(caller);
  normalizePhiIncomings(caller);
  pruneUnreachableBlocks(caller);
  rebuildEdges(caller);
  normalizePhiIncomings(caller);
  return static_cast<int>(caller.blocks.size()) >= originalBlockCount;
}

}  // namespace

std::string InlinePass::name() const { return "inline"; }

PassResult InlinePass::run(Function& function, AnalysisManager& analysisManager) {
  (void)analysisManager;
  if (module_ == nullptr) return PassResult{false};

  rebuildEdges(function);

  bool changed = false;
  bool localChanged = true;
  while (localChanged) {
    localChanged = false;
    for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size()); ++blockIndex) {
      auto& block = function.blocks[blockIndex];
      for (int instIndex = 0; instIndex < static_cast<int>(block.instructions.size()); ++instIndex) {
        if (block.instructions[instIndex].kind != InstKind::Call) continue;
        if (!inlineCallsite(function, *module_, blockIndex, instIndex)) continue;
        changed = true;
        localChanged = true;
        break;
      }
      if (localChanged) break;
    }
  }

  if (changed) {
    rebuildEdges(function);
    normalizePhiIncomings(function);
  }
  return PassResult{changed};
}

}  // namespace midir

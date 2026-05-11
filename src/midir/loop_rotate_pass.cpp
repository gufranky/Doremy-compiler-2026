#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

namespace midir {

namespace {

bool loopContainsBlock(const Loop& loop, int block) {
  return std::binary_search(loop.blocks.begin(), loop.blocks.end(), block);
}

std::string makeUniqueBlockName(const Function& function, const std::string& base) {
  if (function.block_index_by_name.count(base) == 0) return base;
  for (int suffix = 1;; ++suffix) {
    std::string candidate = base + "." + std::to_string(suffix);
    if (function.block_index_by_name.count(candidate) == 0) return candidate;
  }
}

bool isCloneableHeaderInstruction(const Instruction& inst) {
  return inst.kind == InstKind::Copy || inst.kind == InstKind::Binary ||
         inst.kind == InstKind::Unary;
}

bool isHeaderGuardBranch(const Function& function, const Loop& loop, int exitBlock) {
  if (loop.header < 0 || loop.header >= static_cast<int>(function.blocks.size())) {
    return false;
  }
  const auto& header = function.blocks[loop.header];
  if (header.instructions.empty()) return false;
  const auto& term = header.instructions.back();
  if (term.kind != InstKind::Branch || term.operands.size() != 1) return false;

  int insideSuccCount = 0;
  int outsideSuccCount = 0;
  for (int succ : header.succs) {
    if (loopContainsBlock(loop, succ)) {
      ++insideSuccCount;
    } else {
      ++outsideSuccCount;
    }
  }
  if (insideSuccCount != 1 || outsideSuccCount != 1) return false;
  return std::find(header.succs.begin(), header.succs.end(), exitBlock) !=
         header.succs.end();
}

bool canRewriteHeaderUses(const Function& function, int headerIndex, int bodyEntry,
                          int exitIndex,
                          const std::vector<Instruction>& headerInstructions) {
  if (headerInstructions.empty()) return false;

  std::vector<bool> headerDefs(function.next_value_id, false);
  for (const auto& inst : headerInstructions) {
    if (inst.has_result && inst.result_id >= 0 &&
        inst.result_id < static_cast<int>(headerDefs.size())) {
      headerDefs[inst.result_id] = true;
    }
  }

  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size());
       ++blockIndex) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& inst : block.instructions) {
      for (const auto& operand : inst.operands) {
        if (!operand.isSSA() || operand.value_id < 0 ||
            operand.value_id >= static_cast<int>(headerDefs.size()) ||
            !headerDefs[operand.value_id]) {
          continue;
        }
        if (blockIndex != headerIndex) return false;
      }
      for (const auto& incoming : inst.incomings) {
        if (!incoming.value.isSSA() || incoming.value.value_id < 0 ||
            incoming.value.value_id >= static_cast<int>(headerDefs.size()) ||
            !headerDefs[incoming.value.value_id]) {
          continue;
        }
        if (inst.kind != InstKind::Phi) return false;
        if ((blockIndex != bodyEntry && blockIndex != exitIndex) ||
            incoming.pred_block != headerIndex) {
          return false;
        }
      }
    }
  }
  return true;
}

void rewriteSuccessorPhiForRotation(Function& function, int succBlock, int oldPredBlock,
                                    int guardPredBlock,
                                    const std::vector<ValueRef>& initReplacements,
                                    const std::vector<ValueRef>& backedgeReplacements) {
  if (succBlock < 0 || succBlock >= static_cast<int>(function.blocks.size())) return;
  auto& block = function.blocks[succBlock];
  for (auto& inst : block.instructions) {
    if (inst.kind != InstKind::Phi) break;
    auto incomingIt = std::find_if(inst.incomings.begin(), inst.incomings.end(),
                                   [&](const PhiIncoming& incoming) {
                                     return incoming.pred_block == oldPredBlock;
                                   });
    if (incomingIt == inst.incomings.end()) continue;

    const ValueRef originalValue = incomingIt->value;
    incomingIt->value = resolveReplacement(originalValue, backedgeReplacements);
    inst.incomings.push_back(
        PhiIncoming{guardPredBlock, resolveReplacement(originalValue, initReplacements)});
  }
}

bool rotateLoop(Function& function, const Loop& loop) {
  if (loop.header < 0 || loop.preheader < 0 ||
      loop.header >= static_cast<int>(function.blocks.size()) ||
      loop.preheader >= static_cast<int>(function.blocks.size())) {
    return false;
  }
  if (!loop.children.empty()) return false;
  if (!hasSingleLatch(loop) || loop.latches.empty()) return false;
  if (loop.exiting_blocks.size() != 1 || loop.exit_blocks.size() != 1) return false;

  const int headerIndex = loop.header;
  const int preheaderIndex = loop.preheader;
  const int latchIndex = loop.latches.front();
  const int exitingIndex = loop.exiting_blocks.front();
  const int exitIndex = loop.exit_blocks.front();
  if (exitingIndex != headerIndex) return false;
  if (!isHeaderGuardBranch(function, loop, exitIndex)) return false;

  const std::vector<Instruction> originalHeaderInstructions =
      function.blocks[headerIndex].instructions;
  if (originalHeaderInstructions.empty()) return false;
  const Instruction& headerTerm = originalHeaderInstructions.back();
  if (headerTerm.kind != InstKind::Branch || headerTerm.operands.size() != 1) {
    return false;
  }

  int bodyEntry = -1;
  for (int succ : function.blocks[headerIndex].succs) {
    if (loopContainsBlock(loop, succ) && succ != headerIndex) {
      bodyEntry = succ;
      break;
    }
  }
  if (bodyEntry < 0) return false;

  size_t phiCount = 0;
  while (phiCount < originalHeaderInstructions.size() &&
         originalHeaderInstructions[phiCount].kind == InstKind::Phi) {
    ++phiCount;
  }
  if (phiCount == 0 || phiCount >= originalHeaderInstructions.size()) return false;

  for (size_t i = phiCount; i + 1 < originalHeaderInstructions.size(); ++i) {
    if (!isCloneableHeaderInstruction(originalHeaderInstructions[i])) return false;
  }
  if (!canRewriteHeaderUses(function, headerIndex, bodyEntry, exitIndex,
                            originalHeaderInstructions)) {
    return false;
  }

  std::vector<ValueRef> initReplacements(function.next_value_id, ValueRef::Invalid());
  std::vector<ValueRef> backedgeReplacements(function.next_value_id,
                                             ValueRef::Invalid());
  for (size_t i = 0; i < phiCount; ++i) {
    const auto& phi = originalHeaderInstructions[i];
    auto preIt = std::find_if(phi.incomings.begin(), phi.incomings.end(),
                              [&](const PhiIncoming& incoming) {
                                return incoming.pred_block == preheaderIndex;
                              });
    auto latchIt = std::find_if(phi.incomings.begin(), phi.incomings.end(),
                                [&](const PhiIncoming& incoming) {
                                  return incoming.pred_block == latchIndex;
                                });
    if (preIt == phi.incomings.end() || latchIt == phi.incomings.end()) {
      return false;
    }
    initReplacements[phi.result_id] = preIt->value;
    backedgeReplacements[phi.result_id] = latchIt->value;
  }

  BasicBlock guard;
  guard.name = makeUniqueBlockName(function, function.blocks[headerIndex].name + ".guard");
  for (size_t i = phiCount; i + 1 < originalHeaderInstructions.size(); ++i) {
    Instruction cloned = originalHeaderInstructions[i];
    rewriteInstructionOperands(cloned, initReplacements);
    if (cloned.has_result) {
      const int originalResult = originalHeaderInstructions[i].result_id;
      cloned.result_id = function.newValue(cloned.result_type);
      cloned.legacy_result_id = -1;
      initReplacements[originalResult] =
          ValueRef::SSA(cloned.result_id, cloned.result_type);
    }
    guard.instructions.push_back(std::move(cloned));
  }
  Instruction guardTerm = headerTerm;
  rewriteInstructionOperands(guardTerm, initReplacements);
  guard.instructions.push_back(std::move(guardTerm));

  const int guardIndex = static_cast<int>(function.blocks.size());
  function.blocks.push_back(std::move(guard));
  function.block_index_by_name[function.blocks.back().name] = guardIndex;

  rewriteSuccessorPhiForRotation(function, bodyEntry, headerIndex, guardIndex,
                                 initReplacements, backedgeReplacements);
  rewriteSuccessorPhiForRotation(function, exitIndex, headerIndex, guardIndex,
                                 initReplacements, backedgeReplacements);

  std::vector<Instruction> rotatedHeader;
  rotatedHeader.reserve(originalHeaderInstructions.size() - phiCount);
  for (size_t i = phiCount; i < originalHeaderInstructions.size(); ++i) {
    Instruction inst = originalHeaderInstructions[i];
    rewriteInstructionOperands(inst, backedgeReplacements);
    rotatedHeader.push_back(std::move(inst));
  }
  function.blocks[headerIndex].instructions = std::move(rotatedHeader);

  redirectPredecessorTerminator(function, preheaderIndex, headerIndex, guardIndex);

  rebuildEdges(function);
  normalizePhiIncomings(function);
  return true;
}

}  // namespace

std::string LoopRotatePass::name() const { return "loop-rotate"; }

PassResult LoopRotatePass::run(Function& function,
                               AnalysisManager& analysisManager) {
  rebuildEdges(function);

  bool changed = false;
  bool localChanged = true;
  while (localChanged) {
    localChanged = false;
    const LoopInfo& loopInfo = analysisManager.getLoopInfo(function);

    std::vector<int> order(loopInfo.loops.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
      return loopInfo.loops[lhs].depth > loopInfo.loops[rhs].depth;
    });

    for (int loopIndex : order) {
      Loop loop = loopInfo.loops[loopIndex];
      if (!rotateLoop(function, loop)) continue;
      analysisManager.invalidate(function);
      changed = true;
      localChanged = true;
      break;
    }
  }

  if (changed) {
    rebuildEdges(function);
    normalizePhiIncomings(function);
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

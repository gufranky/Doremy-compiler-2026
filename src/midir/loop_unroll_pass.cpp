#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace midir {

namespace {

constexpr int kMaxFullUnrollTripCount = 8;
constexpr int kMaxFullUnrollBodyInsts = 24;
constexpr int kPartialUnrollFactor = 2;
constexpr int kMaxPartialUnrollDuplicatedInsts = 24;

bool debugUnrollEnabled() {
  return std::getenv("SYS2026_DEBUG_UNROLL") != nullptr;
}

void logUnrollDecision(const Function& function, const Loop& loop,
                       const std::string& reason) {
  if (!debugUnrollEnabled()) return;
  std::cerr << "[unroll] " << function.name << " header=" << loop.header
            << " reason=" << reason << "\n";
}

void logTripCountState(const Function& function, const Loop& loop,
                       const CanonicalInductionVariable& iv,
                       const LoopTripCountInfo& tripCount) {
  if (!debugUnrollEnabled()) return;
  std::cerr << "[unroll-trip] " << function.name << " header=" << loop.header
            << " step=" << iv.step << " init_kind="
            << static_cast<int>(iv.init_value.kind) << " has_const="
            << tripCount.has_constant_trip_count << " finite=" << tripCount.is_finite
            << " trip=" << tripCount.trip_count << " compare_block="
            << tripCount.compare_block << " exiting=" << tripCount.exiting_block
            << " exit=" << tripCount.exit_block << " cond="
            << tripCount.condition_value << " compare_op="
            << static_cast<int>(tripCount.compare_op) << " continue_true="
            << tripCount.continue_on_true << " bound_kind="
            << static_cast<int>(tripCount.bound_value.kind) << "\n";
}

struct HeaderPhiInfo {
  int result_id = -1;
  ValueRef init_value = ValueRef::Invalid();
  ValueRef latch_value = ValueRef::Invalid();
};

bool loopContainsBlock(const Loop& loop, int block) {
  return std::binary_search(loop.blocks.begin(), loop.blocks.end(), block);
}

int firstNonPhiIndex(const BasicBlock& block) {
  int index = 0;
  while (index < static_cast<int>(block.instructions.size()) &&
         block.instructions[index].kind == InstKind::Phi) {
    ++index;
  }
  return index;
}

bool isSupportedBodyInstruction(const Instruction& inst) {
  switch (inst.kind) {
    case InstKind::Binary:
    case InstKind::Unary:
    case InstKind::Copy:
    case InstKind::Load:
    case InstKind::Store:
    case InstKind::Call:
      return true;
    default:
      return false;
  }
}

int getLoopBodyEntry(const Function& function, const Loop& loop) {
  if (loop.header < 0 || loop.header >= static_cast<int>(function.blocks.size())) {
    return -1;
  }
  for (int succ : function.blocks[loop.header].succs) {
    if (loopContainsBlock(loop, succ) && succ != loop.header) return succ;
  }
  return -1;
}

bool isCanonicalHeaderBranch(const Function& function, const Loop& loop,
                             const LoopTripCountInfo& tripCount) {
  if (loop.header < 0 || loop.header >= static_cast<int>(function.blocks.size())) {
    return false;
  }
  if (tripCount.exiting_block != loop.header || tripCount.exit_block < 0) return false;

  const auto& header = function.blocks[loop.header];
  if (header.instructions.empty()) return false;
  const auto& term = header.instructions.back();
  if (term.kind != InstKind::Branch || term.operands.size() != 1) return false;
  if (function.block_index_by_name.count(term.true_target) == 0 ||
      function.block_index_by_name.count(term.false_target) == 0) {
    return false;
  }
  return function.block_index_by_name.at(term.false_target) == tripCount.exit_block;
}

bool collectHeaderPhis(const Function& function, const Loop& loop, int latchPred,
                       std::vector<HeaderPhiInfo>& phis) {
  if (loop.header < 0 || loop.header >= static_cast<int>(function.blocks.size())) {
    return false;
  }

  phis.clear();
  const auto& header = function.blocks[loop.header];
  for (const auto& inst : header.instructions) {
    if (inst.kind != InstKind::Phi) break;

    auto preIt = std::find_if(inst.incomings.begin(), inst.incomings.end(),
                              [&](const PhiIncoming& incoming) {
                                return incoming.pred_block == loop.preheader;
                              });
    auto latchIt = std::find_if(inst.incomings.begin(), inst.incomings.end(),
                                [&](const PhiIncoming& incoming) {
                                  return incoming.pred_block == latchPred;
                                });
    if (preIt == inst.incomings.end() || latchIt == inst.incomings.end()) {
      return false;
    }
    phis.push_back(HeaderPhiInfo{inst.result_id, preIt->value, latchIt->value});
  }
  return true;
}

std::unordered_map<int, ValueRef> seedPhiReplacements(
    const std::vector<HeaderPhiInfo>& phis) {
  std::unordered_map<int, ValueRef> replacements;
  for (const auto& phi : phis) {
    replacements[phi.result_id] = phi.init_value;
  }
  return replacements;
}

ValueRef resolveValue(ValueRef value,
                      const std::unordered_map<int, ValueRef>& replacements) {
  while (value.isSSA()) {
    auto it = replacements.find(value.value_id);
    if (it == replacements.end()) break;
    if (!it->second.isValid()) break;
    if (it->second.isSSA() && it->second.value_id == value.value_id) break;
    value = it->second;
  }
  return value;
}

void rewriteClonedInstruction(Instruction& inst,
                              const std::unordered_map<int, ValueRef>& replacements) {
  for (auto& operand : inst.operands) {
    operand = resolveValue(operand, replacements);
  }
  for (auto& incoming : inst.incomings) {
    incoming.value = resolveValue(incoming.value, replacements);
  }
}

void advanceLoopCarriedValues(const std::vector<HeaderPhiInfo>& phis,
                              std::unordered_map<int, ValueRef>& replacements) {
  std::vector<std::pair<int, ValueRef>> nextValues;
  nextValues.reserve(phis.size());
  for (const auto& phi : phis) {
    nextValues.push_back({phi.result_id, resolveValue(phi.latch_value, replacements)});
  }
  for (const auto& entry : nextValues) {
    replacements[entry.first] = entry.second;
  }
}

int countSupportedLinearBlockInstructions(const BasicBlock& block,
                                         int skippedResultId = -1) {
  if (firstNonPhiIndex(block) != 0 || block.instructions.empty()) return -1;

  int bodyInstCount = 0;
  for (size_t i = 0; i + 1 < block.instructions.size(); ++i) {
    const auto& inst = block.instructions[i];
    if (inst.has_result && inst.result_id == skippedResultId) continue;
    if (!isSupportedBodyInstruction(inst)) return -1;
    ++bodyInstCount;
  }
  return bodyInstCount;
}

void appendClonedLinearBlockInstructions(
    Function& function, const std::vector<Instruction>& originalInstructions,
    std::unordered_map<int, ValueRef>& state, std::vector<Instruction>& linearized) {
  for (size_t i = 0; i + 1 < originalInstructions.size(); ++i) {
    const auto& original = originalInstructions[i];
    Instruction cloned = original;
    rewriteClonedInstruction(cloned, state);
    if (cloned.has_result) {
      const int originalResult = original.result_id;
      cloned.result_id = function.newValue(cloned.result_type);
      cloned.legacy_result_id = -1;
      state[originalResult] = ValueRef::SSA(cloned.result_id, cloned.result_type);
    }
    linearized.push_back(std::move(cloned));
  }
}

bool canUnrollSingleBlockLoop(const Function& function, const Loop& loop,
                              const CanonicalInductionVariable& iv,
                              const LoopTripCountInfo& tripCount) {
  if (loop.blocks.size() != 1) return false;
  if (loop.header != loop.latches.front()) return false;
  if (iv.header_block != loop.header || iv.latch_block != loop.header) return false;
  if (!isCanonicalHeaderBranch(function, loop, tripCount)) return false;

  const auto& header = function.blocks[loop.header];
  int bodyInstCount = 0;
  const int start = firstNonPhiIndex(header);
  const int end = static_cast<int>(header.instructions.size()) - 1;
  for (int i = start; i < end; ++i) {
    const auto& inst = header.instructions[i];
    if (inst.has_result && inst.result_id == tripCount.condition_value) continue;
    if (!isSupportedBodyInstruction(inst)) return false;
    ++bodyInstCount;
  }
  return bodyInstCount <= kMaxFullUnrollBodyInsts;
}

bool canUnrollTwoBlockLoop(const Function& function, const Loop& loop,
                           const CanonicalInductionVariable& iv,
                           const LoopTripCountInfo& tripCount) {
  if (loop.blocks.size() != 2) return false;
  if (iv.header_block != loop.header) return false;
  if (!isCanonicalHeaderBranch(function, loop, tripCount)) return false;

  const int bodyBlock = getLoopBodyEntry(function, loop);
  if (bodyBlock < 0 || bodyBlock != loop.latches.front() || bodyBlock != iv.latch_block) {
    return false;
  }
  if (bodyBlock >= static_cast<int>(function.blocks.size())) return false;

  const auto& header = function.blocks[loop.header];
  const int headerStart = firstNonPhiIndex(header);
  if (header.instructions.size() - headerStart != 2) return false;
  if (!header.instructions[headerStart].has_result ||
      header.instructions[headerStart].result_id != tripCount.condition_value) {
    return false;
  }

  const auto& body = function.blocks[bodyBlock];
  const auto bodyInstCount = countSupportedLinearBlockInstructions(body);
  if (bodyInstCount < 0) return false;
  const auto& term = body.instructions.back();
  if (term.kind != InstKind::Jump || term.jump_target != header.name) return false;

  return bodyInstCount <= kMaxFullUnrollBodyInsts;
}

bool canUnrollThreeBlockLoop(const Function& function, const Loop& loop,
                             const CanonicalInductionVariable& iv,
                             const LoopTripCountInfo& tripCount) {
  if (loop.blocks.size() != 3) return false;
  if (iv.header_block != loop.header) return false;
  if (!isCanonicalHeaderBranch(function, loop, tripCount)) return false;

  const int bodyBlock = getLoopBodyEntry(function, loop);
  if (bodyBlock < 0 || bodyBlock == loop.header) return false;
  if (bodyBlock >= static_cast<int>(function.blocks.size())) return false;
  if (loop.latches.front() != iv.latch_block) return false;
  const int latchBlock = loop.latches.front();
  if (latchBlock < 0 || latchBlock >= static_cast<int>(function.blocks.size()) ||
      latchBlock == bodyBlock || latchBlock == loop.header) {
    return false;
  }

  const auto& header = function.blocks[loop.header];
  const int headerStart = firstNonPhiIndex(header);
  if (header.instructions.size() - headerStart != 2) return false;
  if (!header.instructions[headerStart].has_result ||
      header.instructions[headerStart].result_id != tripCount.condition_value) {
    return false;
  }

  const auto& body = function.blocks[bodyBlock];
  const auto& latch = function.blocks[latchBlock];
  const int bodyInstCount = countSupportedLinearBlockInstructions(body);
  const int latchInstCount = countSupportedLinearBlockInstructions(latch);
  if (bodyInstCount < 0 || latchInstCount < 0) return false;
  if (body.instructions.back().kind != InstKind::Jump ||
      function.block_index_by_name.count(body.instructions.back().jump_target) == 0 ||
      function.block_index_by_name.at(body.instructions.back().jump_target) != latchBlock) {
    return false;
  }
  if (latch.instructions.back().kind != InstKind::Jump ||
      latch.instructions.back().jump_target != header.name) {
    return false;
  }
  if (firstNonPhiIndex(latch) != 0) return false;

  int insideSuccCount = 0;
  for (int succ : body.succs) {
    if (loopContainsBlock(loop, succ)) ++insideSuccCount;
  }
  if (insideSuccCount != 1 || body.succs.size() != 1) return false;
  if (latch.succs.size() != 1 || latch.succs.front() != loop.header) return false;

  return bodyInstCount + latchInstCount <= kMaxFullUnrollBodyInsts;
}

bool canPartiallyUnrollTwoBlockLoop(const Function& function, const Loop& loop,
                                    const CanonicalInductionVariable& iv,
                                    const LoopTripCountInfo& tripCount) {
  if (tripCount.trip_count <= kMaxFullUnrollTripCount) return false;
  if (tripCount.trip_count % kPartialUnrollFactor != 0) return false;
  if (!canUnrollTwoBlockLoop(function, loop, iv, tripCount)) return false;

  const int bodyBlock = getLoopBodyEntry(function, loop);
  if (bodyBlock < 0 || bodyBlock >= static_cast<int>(function.blocks.size())) return false;
  const auto& body = function.blocks[bodyBlock];
  const int bodyInstCount = countSupportedLinearBlockInstructions(body);
  if (bodyInstCount < 0) return false;
  return bodyInstCount * kPartialUnrollFactor <= kMaxPartialUnrollDuplicatedInsts;
}

bool canPartiallyUnrollThreeBlockLoop(const Function& function, const Loop& loop,
                                      const CanonicalInductionVariable& iv,
                                      const LoopTripCountInfo& tripCount) {
  if (tripCount.trip_count <= kMaxFullUnrollTripCount) return false;
  if (tripCount.trip_count % kPartialUnrollFactor != 0) return false;
  if (!canUnrollThreeBlockLoop(function, loop, iv, tripCount)) return false;

  const int bodyBlock = getLoopBodyEntry(function, loop);
  const int latchBlock = loop.latches.front();
  if (bodyBlock < 0 || latchBlock < 0 ||
      bodyBlock >= static_cast<int>(function.blocks.size()) ||
      latchBlock >= static_cast<int>(function.blocks.size())) {
    return false;
  }
  const int bodyInstCount =
      countSupportedLinearBlockInstructions(function.blocks[bodyBlock]);
  const int latchInstCount =
      countSupportedLinearBlockInstructions(function.blocks[latchBlock]);
  if (bodyInstCount < 0 || latchInstCount < 0) return false;
  return (bodyInstCount + latchInstCount) * kPartialUnrollFactor <=
         kMaxPartialUnrollDuplicatedInsts;
}

bool canUnrollLoop(const Function& function, const Loop& loop,
                   const CanonicalInductionVariable& iv,
                   const LoopTripCountInfo& tripCount) {
  if (!loop.children.empty()) {
    logUnrollDecision(function, loop, "has-children");
    return false;
  }
  if (!hasSingleLatch(loop) || loop.latches.size() != 1) {
    logUnrollDecision(function, loop, "not-single-latch");
    return false;
  }
  if (loop.header < 0 || loop.header >= static_cast<int>(function.blocks.size())) {
    logUnrollDecision(function, loop, "bad-header");
    return false;
  }
  if (loop.preheader < 0 || loop.preheader >= static_cast<int>(function.blocks.size())) {
    logUnrollDecision(function, loop, "missing-preheader");
    return false;
  }
  if (!tripCount.is_finite || !tripCount.has_constant_trip_count) {
    logUnrollDecision(function, loop, "no-constant-trip-count");
    return false;
  }
  if (!tripCount.continue_on_true) {
    logUnrollDecision(function, loop, "non-canonical-branch-direction");
    return false;
  }
  if (tripCount.trip_count < 0) {
    logUnrollDecision(function, loop, "negative-trip-count");
    return false;
  }

  if (tripCount.trip_count <= kMaxFullUnrollTripCount) {
    if (canUnrollSingleBlockLoop(function, loop, iv, tripCount)) return true;
    if (canUnrollTwoBlockLoop(function, loop, iv, tripCount)) return true;
    if (canUnrollThreeBlockLoop(function, loop, iv, tripCount)) return true;
    logUnrollDecision(function, loop, "unsupported-full-unroll-shape");
    return false;
  }

  logUnrollDecision(function, loop, "trip-count-out-of-range");
  return false;
}

void rewriteExitPhiUses(Function& function, int exitBlock, int predBlock,
                        const std::unordered_map<int, ValueRef>& replacements) {
  if (exitBlock < 0 || exitBlock >= static_cast<int>(function.blocks.size())) return;
  for (auto& inst : function.blocks[exitBlock].instructions) {
    if (inst.kind != InstKind::Phi) break;
    auto incomingIt = std::find_if(inst.incomings.begin(), inst.incomings.end(),
                                   [&](const PhiIncoming& incoming) {
                                     return incoming.pred_block == predBlock;
                                   });
    if (incomingIt == inst.incomings.end()) continue;
    incomingIt->value = resolveValue(incomingIt->value, replacements);
  }
}

bool fullyUnrollSingleBlockLoop(Function& function, const Loop& loop,
                                const LoopTripCountInfo& tripCount) {
  auto& header = function.blocks[loop.header];
  const int exitBlock = tripCount.exit_block;
  std::vector<HeaderPhiInfo> phis;
  if (!collectHeaderPhis(function, loop, loop.header, phis)) return false;

  std::unordered_map<int, ValueRef> state = seedPhiReplacements(phis);
  std::vector<Instruction> unrolledBody;
  const int start = firstNonPhiIndex(header);
  const int end = static_cast<int>(header.instructions.size()) - 1;
  for (int iter = 0; iter < tripCount.trip_count; ++iter) {
    for (int i = start; i < end; ++i) {
      const auto& original = header.instructions[i];
      if (original.has_result && original.result_id == tripCount.condition_value) continue;

      Instruction cloned = original;
      rewriteClonedInstruction(cloned, state);
      if (cloned.has_result) {
        const int originalResult = original.result_id;
        cloned.result_id = function.newValue(cloned.result_type);
        cloned.legacy_result_id = -1;
        state[originalResult] = ValueRef::SSA(cloned.result_id, cloned.result_type);
      }
      unrolledBody.push_back(std::move(cloned));
    }
    advanceLoopCarriedValues(phis, state);
  }

  rewriteExitPhiUses(function, exitBlock, loop.header, state);

  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = function.blocks[exitBlock].name;
  unrolledBody.push_back(std::move(jump));
  header.instructions = std::move(unrolledBody);

  pruneUnreachableBlocks(function);
  rebuildEdges(function);
  normalizePhiIncomings(function);
  return true;
}

bool fullyUnrollTwoBlockLoop(Function& function, const Loop& loop,
                             const LoopTripCountInfo& tripCount) {
  const int bodyBlock = getLoopBodyEntry(function, loop);
  if (bodyBlock < 0 || bodyBlock >= static_cast<int>(function.blocks.size())) return false;
  const int exitBlock = tripCount.exit_block;

  std::vector<HeaderPhiInfo> phis;
  if (!collectHeaderPhis(function, loop, bodyBlock, phis)) return false;
  std::unordered_map<int, ValueRef> state = seedPhiReplacements(phis);

  const auto originalBody = function.blocks[bodyBlock].instructions;
  std::vector<Instruction> linearized;
  for (int iter = 0; iter < tripCount.trip_count; ++iter) {
    appendClonedLinearBlockInstructions(function, originalBody, state, linearized);
    advanceLoopCarriedValues(phis, state);
  }

  rewriteExitPhiUses(function, exitBlock, loop.header, state);

  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = function.blocks[exitBlock].name;
  linearized.push_back(std::move(jump));
  function.blocks[loop.header].instructions = std::move(linearized);

  std::vector<bool> removed(function.blocks.size(), false);
  removed[bodyBlock] = true;
  removeBlocksAndRemap(function, removed);
  pruneUnreachableBlocks(function);
  rebuildEdges(function);
  normalizePhiIncomings(function);
  return true;
}

bool fullyUnrollThreeBlockLoop(Function& function, const Loop& loop,
                               const LoopTripCountInfo& tripCount) {
  const int bodyBlock = getLoopBodyEntry(function, loop);
  if (bodyBlock < 0 || bodyBlock >= static_cast<int>(function.blocks.size())) return false;
  const int latchBlock = loop.latches.front();
  if (latchBlock < 0 || latchBlock >= static_cast<int>(function.blocks.size())) return false;
  const int exitBlock = tripCount.exit_block;

  std::vector<HeaderPhiInfo> phis;
  if (!collectHeaderPhis(function, loop, latchBlock, phis)) return false;
  std::unordered_map<int, ValueRef> state = seedPhiReplacements(phis);

  const auto originalBody = function.blocks[bodyBlock].instructions;
  const auto originalLatch = function.blocks[latchBlock].instructions;
  std::vector<Instruction> linearized;
  for (int iter = 0; iter < tripCount.trip_count; ++iter) {
    appendClonedLinearBlockInstructions(function, originalBody, state, linearized);
    appendClonedLinearBlockInstructions(function, originalLatch, state, linearized);
    advanceLoopCarriedValues(phis, state);
  }

  rewriteExitPhiUses(function, exitBlock, loop.header, state);

  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = function.blocks[exitBlock].name;
  linearized.push_back(std::move(jump));
  function.blocks[loop.header].instructions = std::move(linearized);

  std::vector<bool> removed(function.blocks.size(), false);
  removed[bodyBlock] = true;
  removed[latchBlock] = true;
  removeBlocksAndRemap(function, removed);
  pruneUnreachableBlocks(function);
  rebuildEdges(function);
  normalizePhiIncomings(function);
  return true;
}

void rewriteHeaderPhiBackedge(Function& function, const Loop& loop, int backedgePred,
                             const std::unordered_map<int, ValueRef>& state) {
  if (loop.header < 0 || loop.header >= static_cast<int>(function.blocks.size())) return;
  for (auto& inst : function.blocks[loop.header].instructions) {
    if (inst.kind != InstKind::Phi) break;
    for (auto& incoming : inst.incomings) {
      if (incoming.pred_block != backedgePred) continue;
      incoming.value = resolveValue(incoming.value, state);
    }
  }
}

bool partiallyUnrollTwoBlockLoop(Function& function, const Loop& loop,
                                 const LoopTripCountInfo& tripCount) {
  const int bodyBlock = getLoopBodyEntry(function, loop);
  if (bodyBlock < 0 || bodyBlock >= static_cast<int>(function.blocks.size())) return false;

  std::vector<HeaderPhiInfo> phis;
  if (!collectHeaderPhis(function, loop, bodyBlock, phis)) return false;
  std::unordered_map<int, ValueRef> state = seedPhiReplacements(phis);
  advanceLoopCarriedValues(phis, state);

  const auto originalBody = function.blocks[bodyBlock].instructions;
  std::vector<Instruction> linearized = originalBody;
  linearized.pop_back();
  appendClonedLinearBlockInstructions(function, originalBody, state, linearized);
  advanceLoopCarriedValues(phis, state);

  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = function.blocks[loop.header].name;
  linearized.push_back(std::move(jump));
  function.blocks[bodyBlock].instructions = std::move(linearized);

  rewriteHeaderPhiBackedge(function, loop, bodyBlock, state);

  rebuildEdges(function);
  normalizePhiIncomings(function);
  return true;
}

bool partiallyUnrollThreeBlockLoop(Function& function, const Loop& loop,
                                   const LoopTripCountInfo& tripCount) {
  const int bodyBlock = getLoopBodyEntry(function, loop);
  if (bodyBlock < 0 || bodyBlock >= static_cast<int>(function.blocks.size())) return false;
  const int latchBlock = loop.latches.front();
  if (latchBlock < 0 || latchBlock >= static_cast<int>(function.blocks.size())) return false;

  std::vector<HeaderPhiInfo> phis;
  if (!collectHeaderPhis(function, loop, latchBlock, phis)) return false;
  std::unordered_map<int, ValueRef> state = seedPhiReplacements(phis);
  advanceLoopCarriedValues(phis, state);

  const auto originalBody = function.blocks[bodyBlock].instructions;
  const auto originalLatch = function.blocks[latchBlock].instructions;
  std::vector<Instruction> linearized = originalLatch;
  linearized.pop_back();
  appendClonedLinearBlockInstructions(function, originalBody, state, linearized);
  appendClonedLinearBlockInstructions(function, originalLatch, state, linearized);
  advanceLoopCarriedValues(phis, state);

  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = function.blocks[loop.header].name;
  linearized.push_back(std::move(jump));
  function.blocks[latchBlock].instructions = std::move(linearized);

  rewriteHeaderPhiBackedge(function, loop, latchBlock, state);

  rebuildEdges(function);
  normalizePhiIncomings(function);
  return true;
}

bool fullyUnrollLoop(Function& function, const Loop& loop,
                     const CanonicalInductionVariable& iv,
                     const LoopTripCountInfo& tripCount) {
  if (tripCount.trip_count <= kMaxFullUnrollTripCount) {
    if (canUnrollSingleBlockLoop(function, loop, iv, tripCount)) {
      return fullyUnrollSingleBlockLoop(function, loop, tripCount);
    }
    if (canUnrollTwoBlockLoop(function, loop, iv, tripCount)) {
      return fullyUnrollTwoBlockLoop(function, loop, tripCount);
    }
    if (canUnrollThreeBlockLoop(function, loop, iv, tripCount)) {
      return fullyUnrollThreeBlockLoop(function, loop, tripCount);
    }
  }
  return false;
}

}  // namespace

std::string SimpleLoopUnrollPass::name() const { return "simple-loop-unroll"; }

PassResult SimpleLoopUnrollPass::run(Function& function,
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
      const Loop& loop = loopInfo.loops[loopIndex];
      auto iv = matchCanonicalInductionVariable(function, loop);
      if (!iv.has_value()) continue;
      LoopTripCountInfo tripCount = analyzeLoopTripCount(function, loop);
      logTripCountState(function, loop, *iv, tripCount);
      if (!canUnrollLoop(function, loop, *iv, tripCount)) continue;
      if (!fullyUnrollLoop(function, loop, *iv, tripCount)) continue;

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

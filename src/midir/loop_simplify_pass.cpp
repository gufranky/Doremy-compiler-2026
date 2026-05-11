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

bool valueRefsEqual(const ValueRef& lhs, const ValueRef& rhs) {
  return lhs.kind == rhs.kind && lhs.type == rhs.type &&
         lhs.value_id == rhs.value_id && lhs.int_value == rhs.int_value &&
         lhs.float_value == rhs.float_value &&
         lhs.frame_offset == rhs.frame_offset && lhs.symbol == rhs.symbol;
}

std::string makeUniqueBlockName(const Function& function, const std::string& base) {
  if (function.block_index_by_name.count(base) == 0) return base;
  for (int suffix = 1;; ++suffix) {
    std::string candidate = base + "." + std::to_string(suffix);
    if (function.block_index_by_name.count(candidate) == 0) return candidate;
  }
}

ValueRef buildPreheaderIncomingValue(Function& function,
                                     const std::vector<PhiIncoming>& incomings,
                                     std::vector<Instruction>& preheaderInstructions) {
  if (incomings.empty()) return ValueRef::Invalid();
  const ValueRef& first = incomings.front().value;
  if (std::all_of(incomings.begin(), incomings.end(), [&](const PhiIncoming& incoming) {
        return valueRefsEqual(incoming.value, first);
      })) {
    return first;
  }

  Instruction phi;
  phi.kind = InstKind::Phi;
  phi.result_type = first.type;
  phi.result_id = function.newValue(first.type);
  phi.has_result = true;
  phi.incomings = incomings;
  preheaderInstructions.push_back(std::move(phi));
  return ValueRef::SSA(preheaderInstructions.back().result_id,
                       preheaderInstructions.back().result_type);
}

int splitEdgeWithJump(Function& function, int pred, int succ,
                      const std::string& nameBase) {
  if (pred < 0 || succ < 0 || pred >= static_cast<int>(function.blocks.size()) ||
      succ >= static_cast<int>(function.blocks.size())) {
    return -1;
  }

  const int newIndex = static_cast<int>(function.blocks.size());
  BasicBlock split;
  split.name = makeUniqueBlockName(function, nameBase);

  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = function.blocks[succ].name;
  split.instructions.push_back(std::move(jump));

  function.blocks.push_back(std::move(split));
  function.block_index_by_name[function.blocks.back().name] = newIndex;
  redirectPredecessorTerminator(function, pred, succ, newIndex);
  rebuildEdges(function);
  return newIndex;
}

bool ensurePreheader(Function& function, const Loop& loop) {
  if (loop.header < 0 || loop.header >= static_cast<int>(function.blocks.size())) {
    return false;
  }
  if (getLoopPreheader(function, loop) >= 0) return false;

  std::vector<int> outsidePreds;
  for (int pred : function.blocks[loop.header].preds) {
    if (!loopContainsBlock(loop, pred) &&
        std::find(outsidePreds.begin(), outsidePreds.end(), pred) ==
            outsidePreds.end()) {
      outsidePreds.push_back(pred);
    }
  }
  if (outsidePreds.empty()) return false;

  const int newIndex = static_cast<int>(function.blocks.size());
  BasicBlock preheader;
  preheader.name =
      makeUniqueBlockName(function, function.blocks[loop.header].name + ".preheader");

  auto& header = function.blocks[loop.header];
  for (auto& inst : header.instructions) {
    if (inst.kind != InstKind::Phi) break;

    std::vector<PhiIncoming> insideIncomings;
    std::vector<PhiIncoming> outsideIncomings;
    insideIncomings.reserve(inst.incomings.size());
    outsideIncomings.reserve(inst.incomings.size());
    for (const auto& incoming : inst.incomings) {
      if (loopContainsBlock(loop, incoming.pred_block)) {
        insideIncomings.push_back(incoming);
      } else {
        outsideIncomings.push_back(incoming);
      }
    }
    if (outsideIncomings.empty()) continue;

    ValueRef merged =
        buildPreheaderIncomingValue(function, outsideIncomings, preheader.instructions);
    insideIncomings.push_back(PhiIncoming{newIndex, merged});
    inst.incomings = std::move(insideIncomings);
  }

  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = header.name;
  preheader.instructions.push_back(std::move(jump));

  function.blocks.push_back(std::move(preheader));
  function.block_index_by_name[function.blocks.back().name] = newIndex;

  for (int pred : outsidePreds) {
    redirectPredecessorTerminator(function, pred, loop.header, newIndex);
  }

  rebuildEdges(function);
  normalizePhiIncomings(function);
  return true;
}

bool ensureSingleLatch(Function& function, const Loop& loop) {
  if (loop.header < 0 || loop.header >= static_cast<int>(function.blocks.size()) ||
      loop.latches.size() <= 1) {
    return false;
  }

  const int newIndex = static_cast<int>(function.blocks.size());
  BasicBlock latch;
  latch.name =
      makeUniqueBlockName(function, function.blocks[loop.header].name + ".latch");

  auto& header = function.blocks[loop.header];
  for (auto& inst : header.instructions) {
    if (inst.kind != InstKind::Phi) break;

    std::vector<PhiIncoming> latchIncomings;
    std::vector<PhiIncoming> otherIncomings;
    latchIncomings.reserve(inst.incomings.size());
    otherIncomings.reserve(inst.incomings.size());
    for (const auto& incoming : inst.incomings) {
      if (std::find(loop.latches.begin(), loop.latches.end(), incoming.pred_block) !=
          loop.latches.end()) {
        latchIncomings.push_back(incoming);
      } else {
        otherIncomings.push_back(incoming);
      }
    }
    if (latchIncomings.empty()) continue;

    ValueRef merged =
        buildPreheaderIncomingValue(function, latchIncomings, latch.instructions);
    otherIncomings.push_back(PhiIncoming{newIndex, merged});
    inst.incomings = std::move(otherIncomings);
  }

  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = header.name;
  latch.instructions.push_back(std::move(jump));

  function.blocks.push_back(std::move(latch));
  function.block_index_by_name[function.blocks.back().name] = newIndex;
  for (int pred : loop.latches) {
    redirectPredecessorTerminator(function, pred, loop.header, newIndex);
  }

  rebuildEdges(function);
  normalizePhiIncomings(function);
  return true;
}

bool ensureDedicatedExits(Function& function, const Loop& loop) {
  bool changed = false;
  for (int exitBlock : loop.exit_blocks) {
    if (exitBlock < 0 || exitBlock >= static_cast<int>(function.blocks.size())) {
      continue;
    }

    std::vector<int> insidePreds;
    std::vector<int> outsidePreds;
    for (int pred : function.blocks[exitBlock].preds) {
      if (loopContainsBlock(loop, pred)) {
        insidePreds.push_back(pred);
      } else {
        outsidePreds.push_back(pred);
      }
    }
    if (insidePreds.empty() || outsidePreds.empty()) continue;

    for (int pred : insidePreds) {
      int split = splitEdgeWithJump(function, pred, exitBlock,
                                    function.blocks[exitBlock].name + ".loop.exit");
      if (split < 0) continue;
      rewritePhiForEdgeRedirect(function, exitBlock, pred, std::vector<int>{split});
      rebuildEdges(function);
      normalizePhiIncomings(function);
      changed = true;
    }
  }
  return changed;
}

}  // namespace

std::string LoopSimplifyPass::name() const { return "loop-simplify"; }

PassResult LoopSimplifyPass::run(Function& function,
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
      bool loopChanged = false;
      loopChanged = ensurePreheader(function, loop) || loopChanged;
      loopChanged = ensureSingleLatch(function, loop) || loopChanged;
      loopChanged = ensureDedicatedExits(function, loop) || loopChanged;
      if (!loopChanged) continue;

      rebuildEdges(function);
      normalizePhiIncomings(function);
      analysisManager.invalidate(function);
      localChanged = true;
      changed = true;
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

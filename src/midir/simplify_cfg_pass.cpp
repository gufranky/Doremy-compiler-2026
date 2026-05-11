#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>

namespace midir {

namespace {

bool simplifyBranchOnConstant(BasicBlock& block) {
  if (block.instructions.empty()) return false;
  auto& term = block.instructions.back();
  if (term.kind != InstKind::Branch || term.operands.size() != 1) return false;
  const ValueRef& cond = term.operands.front();
  if (cond.kind != ValueRef::Kind::ImmediateInt) return false;

  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = cond.int_value ? term.true_target : term.false_target;
  block.instructions.back() = std::move(jump);
  return true;
}

bool wouldCreateAmbiguousParallelEdge(const Function& function, int predIndex,
                                    int oldSuccIndex, int newSuccIndex) {
  if (predIndex < 0 || predIndex >= static_cast<int>(function.blocks.size()) ||
      oldSuccIndex < 0 || oldSuccIndex >= static_cast<int>(function.blocks.size()) ||
      newSuccIndex < 0 || newSuccIndex >= static_cast<int>(function.blocks.size())) {
    return true;
  }
  const auto& pred = function.blocks[predIndex];
  if (pred.instructions.empty()) return true;
  const auto& term = pred.instructions.back();
  if (term.kind == InstKind::Jump) return false;
  if (term.kind != InstKind::Branch) return true;

  const std::string& oldName = function.blocks[oldSuccIndex].name;
  const std::string& newName = function.blocks[newSuccIndex].name;
  const bool redirectsTrue = term.true_target == oldName;
  const bool redirectsFalse = term.false_target == oldName;
  if (!redirectsTrue && !redirectsFalse) return true;
  if (redirectsTrue && term.false_target == newName) return true;
  if (redirectsFalse && term.true_target == newName) return true;
  return false;
}

bool mergeTrivialJumpBlock(Function& function, int blockIndex) {
  if (blockIndex < 0 || blockIndex >= static_cast<int>(function.blocks.size())) {
    return false;
  }
  auto& block = function.blocks[blockIndex];
  if (block.instructions.size() != 1 || block.preds.empty()) return false;

  const auto& term = block.instructions.back();
  if (term.kind != InstKind::Jump) return false;

  auto targetIt = function.block_index_by_name.find(term.jump_target);
  if (targetIt == function.block_index_by_name.end()) return false;
  int targetIndex = targetIt->second;
  if (targetIndex == blockIndex) return false;

  std::vector<int> preds = block.preds;
  for (int predIndex : preds) {
    if (wouldCreateAmbiguousParallelEdge(function, predIndex, blockIndex, targetIndex)) {
      return false;
    }
  }

  for (int predIndex : preds) {
    redirectPredecessorTerminator(function, predIndex, blockIndex, targetIndex);
  }
  rewritePhiForEdgeRedirect(function, targetIndex, blockIndex, preds);

  std::vector<bool> removed(function.blocks.size(), false);
  removed[blockIndex] = true;
  return removeBlocksAndRemap(function, removed);
}

bool removeTrampolineBlocks(Function& function) {
  bool changed = false;
  while (true) {
    rebuildEdges(function);
    normalizePhiIncomings(function);

    bool localChanged = false;
    for (int i = 0; i < static_cast<int>(function.blocks.size()); ++i) {
      if (mergeTrivialJumpBlock(function, i)) {
        pruneUnreachableBlocks(function);
        localChanged = true;
        changed = true;
        break;
      }
    }
    if (!localChanged) break;
  }
  return changed;
}

}  // namespace

std::string SimplifyCFGPass::name() const { return "simplify-cfg"; }

PassResult SimplifyCFGPass::run(Function& function,
                                AnalysisManager& analysisManager) {
  bool changed = false;
  for (auto& block : function.blocks) {
    changed = simplifyBranchOnConstant(block) || changed;
  }

  rebuildEdges(function);
  pruneUnreachableBlocks(function);
  normalizePhiIncomings(function);
  changed = removeTrampolineBlocks(function) || changed;

  if (changed) {
    rebuildEdges(function);
    normalizePhiIncomings(function);
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

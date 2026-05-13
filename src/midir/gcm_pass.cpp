#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace midir {

namespace {

struct UseSite {
  int block_index = -1;
  bool is_phi_incoming = false;
  int pred_block = -1;
};

using UseLists = std::vector<std::vector<UseSite>>;

bool isGCMMovableInstruction(const Instruction& inst) {
  if (!inst.has_result || inst.result_id < 0) return false;
  if (hasSideEffects(inst)) return false;
  switch (inst.kind) {
    case InstKind::Binary:
    case InstKind::Unary:
    case InstKind::Copy:
      return true;
    default:
      return false;
  }
}

UseLists buildUseLists(const Function& function) {
  UseLists uses(function.next_value_id);
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size()); ++blockIndex) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& inst : block.instructions) {
      for (const auto& operand : inst.operands) {
        if (!operand.isSSA() || operand.value_id < 0 ||
            operand.value_id >= static_cast<int>(uses.size())) {
          continue;
        }
        uses[operand.value_id].push_back(UseSite{blockIndex, false, -1});
      }
      if (inst.kind != InstKind::Phi) continue;
      for (const auto& incoming : inst.incomings) {
        if (!incoming.value.isSSA() || incoming.value.value_id < 0 ||
            incoming.value.value_id >= static_cast<int>(uses.size())) {
          continue;
        }
        uses[incoming.value.value_id].push_back(
            UseSite{blockIndex, true, incoming.pred_block});
      }
    }
  }
  return uses;
}

std::vector<int> computeDomDepths(const DominatorTree& domTree) {
  std::vector<int> depth(domTree.idom.size(), -1);
  std::function<int(int)> assignDepth = [&](int node) {
    if (node < 0 || node >= static_cast<int>(domTree.idom.size())) return 0;
    if (depth[node] >= 0) return depth[node];
    const int parent = domTree.idom[node];
    if (parent < 0 || parent == node) return depth[node] = 0;
    return depth[node] = assignDepth(parent) + 1;
  };
  for (int i = 0; i < static_cast<int>(domTree.idom.size()); ++i) {
    assignDepth(i);
  }
  return depth;
}

int intersectDom(const DominatorTree& domTree, const std::vector<int>& domDepth,
                 int lhs, int rhs) {
  if (lhs < 0) return rhs;
  if (rhs < 0) return lhs;
  int a = lhs;
  int b = rhs;
  while (a != b) {
    while (a >= 0 && b >= 0 && domDepth[a] > domDepth[b]) {
      int next = domTree.idom[a];
      if (next == a) break;
      a = next;
    }
    while (a >= 0 && b >= 0 && domDepth[b] > domDepth[a]) {
      int next = domTree.idom[b];
      if (next == b) break;
      b = next;
    }
    if (a == b) break;
    int nextA = (a >= 0 && a < static_cast<int>(domTree.idom.size())) ? domTree.idom[a] : -1;
    int nextB = (b >= 0 && b < static_cast<int>(domTree.idom.size())) ? domTree.idom[b] : -1;
    if (nextA == a && nextB == b) return -1;
    a = nextA;
    b = nextB;
  }
  return a;
}

int getBlockLoopDepth(const LoopInfo& loopInfo, int blockIndex) {
  if (blockIndex < 0 || blockIndex >= static_cast<int>(loopInfo.block_loop.size())) {
    return 0;
  }
  const int loopIndex = loopInfo.block_loop[blockIndex];
  if (loopIndex < 0 || loopIndex >= static_cast<int>(loopInfo.loops.size())) {
    return 0;
  }
  return loopInfo.loops[loopIndex].depth;
}

int getUseBlock(const UseSite& use) {
  return use.is_phi_incoming ? use.pred_block : use.block_index;
}

int computeUseLCA(int valueId, const UseLists& useLists, const DominatorTree& domTree,
                  const std::vector<int>& domDepth) {
  if (valueId < 0 || valueId >= static_cast<int>(useLists.size()) ||
      useLists[valueId].empty()) {
    return -1;
  }
  int lca = -1;
  for (const auto& use : useLists[valueId]) {
    const int useBlock = getUseBlock(use);
    if (useBlock < 0) continue;
    lca = intersectDom(domTree, domDepth, lca, useBlock);
    if (lca < 0) return -1;
  }
  return lca;
}

int findInstructionIndexByResult(const BasicBlock& block, int resultId) {
  for (int i = 0; i < static_cast<int>(block.instructions.size()); ++i) {
    const auto& inst = block.instructions[i];
    if (inst.has_result && inst.result_id == resultId) return i;
  }
  return -1;
}

int computeInsertionIndex(const Function& function, int targetBlock,
                          const Instruction& inst,
                          const std::vector<int>& defBlock) {
  const auto& block = function.blocks[targetBlock];
  int phiEnd = 0;
  while (phiEnd < static_cast<int>(block.instructions.size()) &&
         block.instructions[phiEnd].kind == InstKind::Phi) {
    ++phiEnd;
  }

  int limit = static_cast<int>(block.instructions.size());
  if (limit > 0 && isTerminator(block.instructions.back().kind)) {
    --limit;
  }

  int insertionIndex = phiEnd;
  for (const auto& operand : inst.operands) {
    if (!operand.isSSA() || operand.value_id < 0 ||
        operand.value_id >= static_cast<int>(defBlock.size())) {
      continue;
    }
    if (defBlock[operand.value_id] != targetBlock) continue;
    const int defIndex = findInstructionIndexByResult(block, operand.value_id);
    if (defIndex >= 0) insertionIndex = std::max(insertionIndex, defIndex + 1);
  }
  return std::min(insertionIndex, limit);
}

int chooseBestBlock(int defBlockIndex, int useLCA, const DominatorTree& domTree,
                    const std::vector<int>& domDepth,
                    const LoopInfo& loopInfo) {
  if (defBlockIndex < 0 || useLCA < 0) return defBlockIndex;
  int bestBlock = defBlockIndex;
  int bestLoopDepth = getBlockLoopDepth(loopInfo, defBlockIndex);
  int bestDomDepth = domDepth[defBlockIndex];

  for (int block = useLCA; block >= 0; ) {
    if (!dominates(domTree, defBlockIndex, block)) break;
    const int loopDepth = getBlockLoopDepth(loopInfo, block);
    const int blockDomDepth = domDepth[block];
    if (loopDepth > bestLoopDepth ||
        (loopDepth == bestLoopDepth && blockDomDepth > bestDomDepth)) {
      bestBlock = block;
      bestLoopDepth = loopDepth;
      bestDomDepth = blockDomDepth;
    }
    if (block == defBlockIndex) break;
    const int parent = domTree.idom[block];
    if (parent == block) break;
    block = parent;
  }

  return bestBlock;
}

bool moveInstruction(Function& function, int fromBlock, int instIndex, int toBlock,
                     int insertionIndex) {
  if (fromBlock < 0 || toBlock < 0 || fromBlock >= static_cast<int>(function.blocks.size()) ||
      toBlock >= static_cast<int>(function.blocks.size())) {
    return false;
  }
  if (fromBlock == toBlock) return false;

  auto& source = function.blocks[fromBlock];
  if (instIndex < 0 || instIndex >= static_cast<int>(source.instructions.size())) {
    return false;
  }
  Instruction inst = source.instructions[instIndex];
  source.instructions.erase(source.instructions.begin() + instIndex);

  auto& target = function.blocks[toBlock];
  insertionIndex = std::max(0, std::min(insertionIndex,
                                        static_cast<int>(target.instructions.size())));
  target.instructions.insert(target.instructions.begin() + insertionIndex,
                             std::move(inst));
  return true;
}

bool runGCMOnce(Function& function, const DominatorTree& domTree,
                const LoopInfo& loopInfo, const UseLists& useLists,
                const std::vector<int>& domDepth) {
  const std::vector<int> defBlock = buildDefBlocks(function);
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size()); ++blockIndex) {
    auto& block = function.blocks[blockIndex];
    for (int instIndex = 0; instIndex < static_cast<int>(block.instructions.size()); ++instIndex) {
      const Instruction& inst = block.instructions[instIndex];
      if (!isGCMMovableInstruction(inst)) continue;
      const int valueId = inst.result_id;
      if (valueId < 0 || valueId >= static_cast<int>(defBlock.size())) continue;
      if (defBlock[valueId] != blockIndex) continue;
      const int useLCA = computeUseLCA(valueId, useLists, domTree, domDepth);
      if (useLCA < 0 || useLCA == blockIndex) continue;
      const int targetBlock = chooseBestBlock(blockIndex, useLCA, domTree, domDepth, loopInfo);
      if (targetBlock < 0 || targetBlock == blockIndex) continue;
      const int insertionIndex = computeInsertionIndex(function, targetBlock, inst, defBlock);
      if (moveInstruction(function, blockIndex, instIndex, targetBlock, insertionIndex)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

std::string GCMPass::name() const { return "gcm"; }

PassResult GCMPass::run(Function& function, AnalysisManager& analysisManager) {
  rebuildEdges(function);
  const DominatorTree& domTree = analysisManager.getDominatorTree(function);
  const LoopInfo& loopInfo = analysisManager.getLoopInfo(function);
  const UseLists useLists = buildUseLists(function);
  const std::vector<int> domDepth = computeDomDepths(domTree);

  bool changed = false;
  while (runGCMOnce(function, domTree, loopInfo, useLists, domDepth)) {
    changed = true;
  }

  if (changed) {
    rebuildEdges(function);
    normalizePhiIncomings(function);
  }
  return PassResult{changed};
}

}  // namespace midir

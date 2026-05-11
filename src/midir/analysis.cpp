#include "analysis.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <unordered_map>

namespace midir {

namespace {

std::vector<int> collectNaturalLoop(const Function& function, int header,
                                    int latch) {
  std::vector<int> blocks;
  if (header < 0 || latch < 0 ||
      header >= static_cast<int>(function.blocks.size()) ||
      latch >= static_cast<int>(function.blocks.size())) {
    return blocks;
  }

  std::vector<bool> in_loop(function.blocks.size(), false);
  std::vector<int> stack;
  in_loop[header] = true;
  blocks.push_back(header);
  if (!in_loop[latch]) {
    in_loop[latch] = true;
    blocks.push_back(latch);
    stack.push_back(latch);
  }

  while (!stack.empty()) {
    int block = stack.back();
    stack.pop_back();
    for (int pred : function.blocks[block].preds) {
      if (pred < 0 || pred >= static_cast<int>(function.blocks.size()) ||
          in_loop[pred]) {
        continue;
      }
      in_loop[pred] = true;
      blocks.push_back(pred);
      if (pred != header) {
        stack.push_back(pred);
      }
    }
  }

  std::sort(blocks.begin(), blocks.end());
  return blocks;
}

bool loopContainsBlock(const Loop& loop, int block) {
  return std::find(loop.blocks.begin(), loop.blocks.end(), block) !=
         loop.blocks.end();
}

std::vector<int> collectExitBlocks(const Function& function, const Loop& loop) {
  std::vector<int> exits;
  for (int block : loop.blocks) {
    for (int succ : function.blocks[block].succs) {
      if (loopContainsBlock(loop, succ)) continue;
      if (std::find(exits.begin(), exits.end(), succ) == exits.end()) {
        exits.push_back(succ);
      }
    }
  }
  std::sort(exits.begin(), exits.end());
  return exits;
}

std::vector<int> collectExitingBlocks(const Function& function, const Loop& loop) {
  std::vector<int> exiting;
  for (int block : loop.blocks) {
    const auto& succs = function.blocks[block].succs;
    if (std::any_of(succs.begin(), succs.end(),
                    [&](int succ) { return !loopContainsBlock(loop, succ); })) {
      exiting.push_back(block);
    }
  }
  std::sort(exiting.begin(), exiting.end());
  return exiting;
}

}  // namespace

DominatorTree buildDominatorTree(const Function& function) {
  DominatorTree tree;
  const int n = static_cast<int>(function.blocks.size());
  tree.idom.assign(n, -1);
  tree.children.assign(n, {});
  tree.frontier.assign(n, {});
  if (n == 0 || function.entry_block < 0) {
    return tree;
  }

  std::vector<int> postorder;
  postorder.reserve(n);
  std::vector<bool> visited(n, false);

  std::function<void(int)> dfs = [&](int block) {
    visited[block] = true;
    for (int succ : function.blocks[block].succs) {
      if (succ >= 0 && succ < n && !visited[succ]) {
        dfs(succ);
      }
    }
    postorder.push_back(block);
  };
  dfs(function.entry_block);

  std::vector<int> rpo(postorder.rbegin(), postorder.rend());
  std::vector<int> rpoIndex(n, -1);
  for (int i = 0; i < static_cast<int>(rpo.size()); ++i) {
    rpoIndex[rpo[i]] = i;
  }

  auto intersect = [&](int b1, int b2) {
    while (b1 != b2) {
      while (rpoIndex[b1] > rpoIndex[b2]) b1 = tree.idom[b1];
      while (rpoIndex[b2] > rpoIndex[b1]) b2 = tree.idom[b2];
    }
    return b1;
  };

  tree.idom[function.entry_block] = function.entry_block;
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 1; i < rpo.size(); ++i) {
      int block = rpo[i];
      int newIdom = -1;
      for (int pred : function.blocks[block].preds) {
        if (pred < 0 || pred >= n || tree.idom[pred] == -1) continue;
        if (newIdom == -1) {
          newIdom = pred;
        } else {
          newIdom = intersect(pred, newIdom);
        }
      }
      if (newIdom != -1 && tree.idom[block] != newIdom) {
        tree.idom[block] = newIdom;
        changed = true;
      }
    }
  }

  for (int block : rpo) {
    if (block == function.entry_block) continue;
    int parent = tree.idom[block];
    if (parent >= 0) {
      tree.children[parent].push_back(block);
    }
  }

  for (int block : rpo) {
    if (function.blocks[block].preds.size() < 2) continue;
    for (int pred : function.blocks[block].preds) {
      int runner = pred;
      while (runner != -1 && runner != tree.idom[block]) {
        auto& frontier = tree.frontier[runner];
        if (std::find(frontier.begin(), frontier.end(), block) == frontier.end()) {
          frontier.push_back(block);
        }
        if (runner == tree.idom[runner]) break;
        runner = tree.idom[runner];
      }
    }
  }

  return tree;
}

bool dominates(const DominatorTree& domTree, int dominator, int node) {
  if (dominator < 0 || node < 0 || node >= static_cast<int>(domTree.idom.size())) {
    return false;
  }
  int current = node;
  while (current >= 0 && current < static_cast<int>(domTree.idom.size())) {
    if (current == dominator) return true;
    if (domTree.idom[current] == current) break;
    current = domTree.idom[current];
  }
  return false;
}

LoopInfo buildLoopInfo(const Function& function, const DominatorTree& domTree) {
  LoopInfo info;
  info.block_loop.assign(function.blocks.size(), -1);

  std::unordered_map<int, int> loop_by_header;
  for (int tail = 0; tail < static_cast<int>(function.blocks.size()); ++tail) {
    for (int succ : function.blocks[tail].succs) {
      if (!dominates(domTree, succ, tail)) continue;
      std::vector<int> blocks = collectNaturalLoop(function, succ, tail);
      auto [it, inserted] = loop_by_header.emplace(succ, static_cast<int>(info.loops.size()));
      if (inserted) {
        Loop loop;
        loop.header = succ;
        loop.blocks = std::move(blocks);
        loop.latches.push_back(tail);
        loop.preheader = getLoopPreheader(function, loop);
        loop.exiting_blocks = collectExitingBlocks(function, loop);
        loop.exit_blocks = collectExitBlocks(function, loop);
        info.loops.push_back(std::move(loop));
        continue;
      }

      Loop& loop = info.loops[it->second];
      if (std::find(loop.latches.begin(), loop.latches.end(), tail) == loop.latches.end()) {
        loop.latches.push_back(tail);
      }
      for (int block : blocks) {
        if (!loopContainsBlock(loop, block)) {
          loop.blocks.push_back(block);
        }
      }
      std::sort(loop.blocks.begin(), loop.blocks.end());
      loop.preheader = getLoopPreheader(function, loop);
      loop.exiting_blocks = collectExitingBlocks(function, loop);
      loop.exit_blocks = collectExitBlocks(function, loop);
    }
  }

  std::vector<int> order(info.loops.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
    return info.loops[lhs].blocks.size() < info.loops[rhs].blocks.size();
  });

  for (int loop_index : order) {
    Loop& loop = info.loops[loop_index];
    for (size_t candidate = 0; candidate < info.loops.size(); ++candidate) {
      if (static_cast<int>(candidate) == loop_index) continue;
      const Loop& parent = info.loops[candidate];
      if (parent.blocks.size() <= loop.blocks.size()) continue;
      bool contained = std::all_of(loop.blocks.begin(), loop.blocks.end(), [&](int block) {
        return loopContainsBlock(parent, block);
      });
      if (!contained) continue;
      if (loop.parent == -1 ||
          info.loops[loop.parent].blocks.size() > parent.blocks.size()) {
        loop.parent = static_cast<int>(candidate);
      }
    }
  }

  for (size_t i = 0; i < info.loops.size(); ++i) {
    Loop& loop = info.loops[i];
    loop.depth = 1;
    if (loop.parent != -1) {
      info.loops[loop.parent].children.push_back(static_cast<int>(i));
    }
  }

  std::function<int(int)> assignDepth = [&](int loop_index) {
    Loop& loop = info.loops[loop_index];
    if (loop.parent == -1) return loop.depth = 1;
    return loop.depth = assignDepth(loop.parent) + 1;
  };
  for (size_t i = 0; i < info.loops.size(); ++i) {
    assignDepth(static_cast<int>(i));
  }

  std::vector<int> sorted_loops(order.rbegin(), order.rend());
  for (int loop_index : sorted_loops) {
    Loop& loop = info.loops[loop_index];
    loop.preheader = getLoopPreheader(function, loop);
    loop.exiting_blocks = collectExitingBlocks(function, loop);
    loop.exit_blocks = collectExitBlocks(function, loop);
    for (int block : loop.blocks) {
      if (block >= 0 && block < static_cast<int>(info.block_loop.size()) &&
          info.block_loop[block] == -1) {
        info.block_loop[block] = loop_index;
      }
    }
  }

  return info;
}

int getLoopPreheader(const Function& function, const Loop& loop) {
  std::vector<int> outside_preds;
  for (int pred : function.blocks[loop.header].preds) {
    if (!loopContainsBlock(loop, pred)) {
      outside_preds.push_back(pred);
    }
  }
  if (outside_preds.size() != 1) return -1;

  int preheader = outside_preds.front();
  const auto& succs = function.blocks[preheader].succs;
  if (succs.size() != 1 || succs.front() != loop.header) {
    return -1;
  }
  return preheader;
}

bool hasSingleLatch(const Loop& loop) { return loop.latches.size() == 1; }

std::vector<int> getLoopExitBlocks(const Function& function, const Loop& loop) {
  std::vector<int> exits;
  for (int block : loop.blocks) {
    for (int succ : function.blocks[block].succs) {
      if (loopContainsBlock(loop, succ)) continue;
      if (std::find(exits.begin(), exits.end(), succ) == exits.end()) {
        exits.push_back(succ);
      }
    }
  }
  return exits;
}

}  // namespace midir

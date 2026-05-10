#include "analysis.h"

#include <algorithm>
#include <functional>

namespace midir {

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

}  // namespace midir

#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <vector>

#include "midir.h"

namespace midir {

struct DominatorTree {
  std::vector<int> idom;
  std::vector<std::vector<int>> children;
  std::vector<std::vector<int>> frontier;
};

DominatorTree buildDominatorTree(const Function& function);
bool dominates(const DominatorTree& domTree, int dominator, int node);

}  // namespace midir

#endif

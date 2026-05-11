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

struct Loop {
  int header = -1;
  int preheader = -1;
  int parent = -1;
  int depth = 0;
  std::vector<int> latches;
  std::vector<int> exiting_blocks;
  std::vector<int> exit_blocks;
  std::vector<int> blocks;
  std::vector<int> children;
};

struct LoopInfo {
  std::vector<Loop> loops;
  std::vector<int> block_loop;
};

DominatorTree buildDominatorTree(const Function& function);
bool dominates(const DominatorTree& domTree, int dominator, int node);
LoopInfo buildLoopInfo(const Function& function, const DominatorTree& domTree);
int getLoopPreheader(const Function& function, const Loop& loop);
bool hasSingleLatch(const Loop& loop);
std::vector<int> getLoopExitBlocks(const Function& function, const Loop& loop);

}  // namespace midir

#endif

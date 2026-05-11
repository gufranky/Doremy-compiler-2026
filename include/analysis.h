#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <optional>
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

struct CanonicalInductionVariable {
  int phi_value = -1;
  int preheader_block = -1;
  int header_block = -1;
  int latch_block = -1;
  int update_result = -1;
  int step = 0;
  ValueRef init_value = ValueRef::Invalid();
  std::vector<int> latch_blocks;
};

struct LoopTripCountInfo {
  bool is_finite = false;
  bool has_constant_trip_count = false;
  int trip_count = 0;
  int compare_block = -1;
  int exiting_block = -1;
  int exit_block = -1;
  int condition_value = -1;
  ValueRef bound_value = ValueRef::Invalid();
  ir::BinaryOp compare_op = ir::BinaryOp::Add;
  bool continue_on_true = true;
};

DominatorTree buildDominatorTree(const Function& function);
bool dominates(const DominatorTree& domTree, int dominator, int node);
LoopInfo buildLoopInfo(const Function& function, const DominatorTree& domTree);
int getLoopPreheader(const Function& function, const Loop& loop);
bool hasSingleLatch(const Loop& loop);
std::vector<int> getLoopExitBlocks(const Function& function, const Loop& loop);
std::optional<CanonicalInductionVariable> matchCanonicalInductionVariable(
    const Function& function, const Loop& loop);
LoopTripCountInfo analyzeLoopTripCount(const Function& function, const Loop& loop);

}  // namespace midir

#endif

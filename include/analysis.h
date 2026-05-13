#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <optional>
#include <string>
#include <unordered_map>
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

struct MemoryLocation {
  enum class BaseKind { Invalid, Global, Frame, Stack, Param, Unknown };

  BaseKind base_kind = BaseKind::Invalid;
  std::string symbol;
  int frame_offset = 0;
  int param_index = -1;
  bool offset_known = false;
  int offset = 0;
  int access_size = 0;

  bool isIdentifiable() const;
  bool sameBase(const MemoryLocation& other) const;
};

struct MemoryAnalysisCache {
  explicit MemoryAnalysisCache(int value_count = 0)
      : constant_offset_known(value_count, 0),
        constant_offset_in_progress(value_count, 0),
        constant_offset_values(value_count, 0),
        location_known(value_count, 0),
        location_in_progress(value_count, 0),
        locations(value_count) {}

  std::vector<char> constant_offset_known;
  std::vector<char> constant_offset_in_progress;
  std::vector<int> constant_offset_values;
  std::vector<char> location_known;
  std::vector<char> location_in_progress;
  std::vector<MemoryLocation> locations;
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

int typeStoreSize(Type type);
std::string memoryLocationKey(const MemoryLocation& location);
bool rangesOverlap(const MemoryLocation& lhs, const MemoryLocation& rhs);
std::vector<int> buildDefBlocks(const Function& function);
std::unordered_map<int, Instruction> buildInstructionDefs(const Function& function);
int findPointerParamIndex(const Function& function, int valueId);
bool computeConstantOffset(const ValueRef& value,
                           const std::vector<int>& defBlock,
                           const std::unordered_map<int, Instruction>& defs,
                           int& offset);
bool computeConstantOffset(const ValueRef& value,
                           const std::vector<int>& defBlock,
                           const std::unordered_map<int, Instruction>& defs,
                           int& offset,
                           MemoryAnalysisCache* cache);
MemoryLocation analyzeMemoryLocation(
    const Function& function, const ValueRef& value,
    const std::vector<int>& defBlock,
    const std::unordered_map<int, Instruction>& defs);
MemoryLocation analyzeMemoryLocation(
    const Function& function, const ValueRef& value,
    const std::vector<int>& defBlock,
    const std::unordered_map<int, Instruction>& defs,
    MemoryAnalysisCache* cache);

}  // namespace midir

#endif

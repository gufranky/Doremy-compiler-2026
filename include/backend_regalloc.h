#ifndef BACKEND_REGALLOC_H
#define BACKEND_REGALLOC_H

#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend_liveness.h"
#include "ir.h"

namespace regalloc {

// Interference graph: nodes are virtual registers, edges indicate conflicts
struct InterferenceGraph {
  std::set<int> nodes;               // All virtual registers
  std::map<int, std::set<int>> adj;  // Adjacency list

  void addNode(int vreg);
  void addEdge(int u, int v);
  int degree(int node) const;
  std::set<int> neighbors(int node) const;
  void removeNode(int node);
};

// Result of register allocation
struct RegAllocResult {
  std::unordered_map<int, int> allocation;  // vreg -> physical register
  std::set<int> spilledVRegs;  // Registers that need to be spilled to stack
  bool needsRetry;             // True if spilling occurred and we need to retry
};

// Graph coloring register allocator
class GraphColoringAllocator {
 public:
  // Number of allocatable physical registers. Reserve t5/t6 as scratch;
  // allocate t0-t4 (caller-saved) and s0-s7 (callee-saved).
  static constexpr int NUM_COLORS = 13;

  // Perform register allocation
  // Returns allocation map and potentially spilled registers
  RegAllocResult allocate(const ir::IRFunction& func,
                          const LivenessResult& liveness);

 private:
  // Build interference graph from liveness information
  InterferenceGraph buildInterferenceGraph(const ir::IRFunction& func,
                                           const LivenessResult& liveness);

  // Simplify phase: remove nodes with degree < K
  std::vector<int> simplify(InterferenceGraph& graph, int K,
                            const std::unordered_map<int, int>& spillCost,
                            std::unordered_set<int>& spilled, bool& needsRetry);

  // Select spill candidate (heuristic: choose highest degree)
  int selectSpillCandidate(const InterferenceGraph& graph,
                           const std::unordered_map<int, int>& spillCost);

  // Coloring phase: assign colors to nodes
  bool colorGraph(const std::vector<int>& stack,
                  const InterferenceGraph& original,
                  const std::unordered_map<int, bool>& liveAcrossCall,
                  std::unordered_map<int, int>& colors, std::set<int>& spilled);
};

// Helper to map virtual registers to RISC-V physical registers
// Allocatable: t0-t4 (caller-saved), s0-s7 (callee-saved)
// Scratch (not allocated): t5, t6
struct RISCVRegMap {
  static std::string physicalRegName(int color);
  static int colorToPhysicalReg(int color);
};

}  // namespace regalloc

#endif  // BACKEND_REGALLOC_H

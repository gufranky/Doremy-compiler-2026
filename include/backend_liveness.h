#ifndef BACKEND_LIVENESS_H
#define BACKEND_LIVENESS_H

#include <unordered_set>
#include <vector>

#include "cfg.h"

// Liveness analysis on IR (virtual registers) using CFG basic blocks.
struct LivenessResult {
  std::vector<std::unordered_set<int>> liveIn;   // per instruction index
  std::vector<std::unordered_set<int>> liveOut;  // per instruction index
  std::vector<ir::Instruction*> order;           // same indexing
};

LivenessResult AnalyzeLiveness(const ir::IRFunction& func);

#endif  // BACKEND_LIVENESS_H
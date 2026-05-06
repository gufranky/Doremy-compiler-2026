#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include <cstdint>

#include "ir.h"

namespace ir {

enum class OptPass : int {
  LocalMemOpt = 0,
  GlobalVarConst,
  ConstantFold,
  AlgebraicSimplify,
  ModSimplify,
  LoopIVModElim,
  StrengthReduction,
  CopyPropagate,
  BlockGVN,
  CommonSubexpr,
  DeadCodeElim,
  DeadStoreElim,
  SimplifyCFG,
  Count,
};

constexpr std::uint64_t kAllOptPasses =
    (1ULL << static_cast<int>(OptPass::Count)) - 1ULL;

struct OptimizeConfig {
  std::uint64_t passMask = kAllOptPasses;
  int stopAfter = -1;
  bool disableIpa = false;
};

constexpr std::uint64_t optPassBit(OptPass pass) {
  return 1ULL << static_cast<int>(pass);
}

// Run middle-end optimizations on a function/program.
// The default configuration keeps the full pipeline unchanged, while
// OptimizeConfig can selectively disable passes for debugging.
void OptimizeFunction(IRFunction& func, const OptimizeConfig& config = {});
void OptimizeProgram(IRProgram& program, const OptimizeConfig& config = {});

}  // namespace ir

#endif  // OPTIMIZER_H
#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "ir.h"

namespace ir {

// Run middle-end optimizations (p01-p05) on a function/program.
// The pipeline performs constant folding, algebraic simplification,
// copy propagation, common subexpression elimination, and dead code removal.
void OptimizeFunction(IRFunction& func);
void OptimizeProgram(IRProgram& program);

}  // namespace ir

#endif  // OPTIMIZER_H
#ifndef CFG_H
#define CFG_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ir.h"

namespace ir {

// Forward declarations
struct BasicBlock;

// Control flow graph for a single IR function.
struct ControlFlowGraph {
  IRFunction* function = nullptr;  // Non-owning pointer to source IR
  std::vector<std::unique_ptr<BasicBlock>> blocks;  // Owns basic blocks
  BasicBlock* entry = nullptr;                      // Entry basic block

  // Build a CFG by partitioning the function's linear IR into basic blocks
  // and linking successors/predecessors.
  static ControlFlowGraph Build(IRFunction* func);
};

// A basic block groups a straight-line sequence of IR instructions.
struct BasicBlock {
  std::string name;                        // Label name or synthesized id
  std::vector<Instruction*> instructions;  // Non-owning pointers
  std::vector<BasicBlock*> preds;          // Predecessor edges
  std::vector<BasicBlock*> succs;          // Successor edges

  // True if the block ends with Branch/Jump/Return.
  bool hasTerminator() const;
};

}  // namespace ir

#endif  // CFG_H
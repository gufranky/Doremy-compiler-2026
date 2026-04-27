#include "cfg.h"

#include <unordered_map>

namespace ir {

namespace {

BasicBlock* lookupBlock(
    const std::string& label,
    const std::unordered_map<std::string, BasicBlock*>& labelToBlock) {
  auto it = labelToBlock.find(label);
  return it == labelToBlock.end() ? nullptr : it->second;
}

void addEdge(BasicBlock* from, BasicBlock* to) {
  if (!from || !to) return;
  auto exists = [&](const std::vector<BasicBlock*>& vec, BasicBlock* blk) {
    for (auto* b : vec) {
      if (b == blk) return true;
    }
    return false;
  };
  if (!exists(from->succs, to)) from->succs.push_back(to);
  if (!exists(to->preds, from)) to->preds.push_back(from);
}

}  // namespace

bool BasicBlock::hasTerminator() const {
  if (instructions.empty()) return false;
  InstKind k = instructions.back()->kind;
  return k == InstKind::Branch || k == InstKind::Jump || k == InstKind::Return;
}

ControlFlowGraph ControlFlowGraph::Build(IRFunction* func) {
  ControlFlowGraph cfg;
  cfg.function = func;

  if (!func || func->instructions.empty()) {
    return cfg;
  }

  const auto& insts = func->instructions;
  const size_t n = insts.size();

  std::unordered_map<std::string, size_t> labelToIndex;
  for (size_t i = 0; i < n; ++i) {
    if (auto* lbl = dynamic_cast<LabelInst*>(insts[i].get())) {
      labelToIndex[lbl->label] = i;
    }
  }

  std::vector<bool> isLeader(n, false);
  isLeader[0] = true;
  for (size_t i = 0; i < n; ++i) {
    Instruction* inst = insts[i].get();
    if (inst->kind == InstKind::Label) {
      isLeader[i] = true;
    }
    if (inst->kind == InstKind::Branch) {
      auto* br = static_cast<BranchInst*>(inst);
      auto itT = labelToIndex.find(br->trueLabel);
      if (itT != labelToIndex.end()) isLeader[itT->second] = true;
      auto itF = labelToIndex.find(br->falseLabel);
      if (itF != labelToIndex.end()) isLeader[itF->second] = true;
      if (i + 1 < n) isLeader[i + 1] = true;
    } else if (inst->kind == InstKind::Jump) {
      auto* j = static_cast<JumpInst*>(inst);
      auto it = labelToIndex.find(j->target);
      if (it != labelToIndex.end()) isLeader[it->second] = true;
      if (i + 1 < n) isLeader[i + 1] = true;
    } else if (inst->kind == InstKind::Return) {
      if (i + 1 < n) isLeader[i + 1] = true;
    }
  }

  std::vector<size_t> leaderIdx;
  leaderIdx.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    if (isLeader[i]) leaderIdx.push_back(i);
  }

  std::unordered_map<std::string, BasicBlock*> labelToBlock;
  for (size_t li = 0; li < leaderIdx.size(); ++li) {
    size_t start = leaderIdx[li];
    size_t end =
        (li + 1 < leaderIdx.size()) ? (leaderIdx[li + 1] - 1) : (n - 1);

    auto block = std::make_unique<BasicBlock>();

    // Name the block using the first label if present; otherwise synthesize.
    if (insts[start]->kind == InstKind::Label) {
      auto* lbl = static_cast<LabelInst*>(insts[start].get());
      block->name = lbl->label;
    } else {
      block->name = "bb" + std::to_string(cfg.blocks.size());
    }

    for (size_t i = start; i <= end; ++i) {
      block->instructions.push_back(insts[i].get());
    }

    BasicBlock* blockPtr = block.get();
    if (insts[start]->kind == InstKind::Label) {
      auto* lbl = static_cast<LabelInst*>(insts[start].get());
      labelToBlock[lbl->label] = blockPtr;
    }

    cfg.blocks.push_back(std::move(block));
  }

  if (!cfg.blocks.empty()) cfg.entry = cfg.blocks.front().get();

  for (size_t i = 0; i < cfg.blocks.size(); ++i) {
    BasicBlock* blk = cfg.blocks[i].get();
    if (blk->instructions.empty()) continue;

    Instruction* last = blk->instructions.back();
    switch (last->kind) {
      case InstKind::Branch: {
        auto* br = static_cast<BranchInst*>(last);
        addEdge(blk, lookupBlock(br->trueLabel, labelToBlock));
        addEdge(blk, lookupBlock(br->falseLabel, labelToBlock));
        break;
      }
      case InstKind::Jump: {
        auto* j = static_cast<JumpInst*>(last);
        addEdge(blk, lookupBlock(j->target, labelToBlock));
        break;
      }
      case InstKind::Return:
        break;  // No successors
      default: {
        // Fallthrough to the next block if it exists
        if (i + 1 < cfg.blocks.size()) {
          addEdge(blk, cfg.blocks[i + 1].get());
        }
        break;
      }
    }
  }

  return cfg;
}

}  // namespace ir

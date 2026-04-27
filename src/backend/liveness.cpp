#include <unordered_map>

#include "backend_liveness.h"

using namespace ir;

namespace {

bool isDef(const Instruction* inst, int& defReg) {
  defReg = -1;
  switch (inst->kind) {
    case InstKind::Binary:
      defReg = static_cast<const BinaryInst*>(inst)->dest;
      return true;
    case InstKind::Unary:
      defReg = static_cast<const UnaryInst*>(inst)->dest;
      return true;
    case InstKind::Copy:
      defReg = static_cast<const CopyInst*>(inst)->dest;
      return true;
    case InstKind::Load:
      defReg = static_cast<const LoadInst*>(inst)->dest;
      return true;
    case InstKind::Call: {
      auto* c = static_cast<const CallInst*>(inst);
      if (c->hasDest) {
        defReg = c->dest;
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

void addUse(const Operand& op, std::unordered_set<int>& uses) {
  if (op.isVReg()) uses.insert(op.vregId);
}

void collectUses(const Instruction* inst, std::unordered_set<int>& uses) {
  switch (inst->kind) {
    case InstKind::Binary: {
      auto* b = static_cast<const BinaryInst*>(inst);
      addUse(b->lhs, uses);
      addUse(b->rhs, uses);
      break;
    }
    case InstKind::Unary: {
      auto* u = static_cast<const UnaryInst*>(inst);
      addUse(u->operand, uses);
      break;
    }
    case InstKind::Copy: {
      auto* c = static_cast<const CopyInst*>(inst);
      addUse(c->src, uses);
      break;
    }
    case InstKind::Load: {
      auto* l = static_cast<const LoadInst*>(inst);
      addUse(l->addr, uses);
      break;
    }
    case InstKind::Store: {
      auto* s = static_cast<const StoreInst*>(inst);
      addUse(s->src, uses);
      addUse(s->addr, uses);
      break;
    }
    case InstKind::Branch: {
      auto* br = static_cast<const BranchInst*>(inst);
      addUse(br->cond, uses);
      break;
    }
    case InstKind::Call: {
      auto* c = static_cast<const CallInst*>(inst);
      for (const auto& a : c->args) addUse(a, uses);
      break;
    }
    case InstKind::Return: {
      auto* r = static_cast<const ReturnInst*>(inst);
      if (r->hasValue) addUse(r->value, uses);
      break;
    }
    default:
      break;
  }
}

}  // namespace

LivenessResult AnalyzeLiveness(const IRFunction& func) {
  LivenessResult res;
  res.order.reserve(func.instructions.size());
  for (const auto& inst : func.instructions) res.order.push_back(inst.get());

  size_t n = res.order.size();
  res.liveIn.resize(n);
  res.liveOut.resize(n);

  // Build CFG for block successors and map instruction -> index
  ControlFlowGraph cfg =
      ControlFlowGraph::Build(const_cast<IRFunction*>(&func));
  std::unordered_map<const Instruction*, size_t> idx;
  for (size_t i = 0; i < n; ++i) idx[res.order[i]] = i;

  // Map block to first instruction index
  std::unordered_map<const BasicBlock*, size_t> blockFirst;
  for (auto& bptr : cfg.blocks) {
    const BasicBlock* b = bptr.get();
    if (!b->instructions.empty()) blockFirst[b] = idx[b->instructions.front()];
  }

  // Precompute per-instruction succ list
  std::vector<std::vector<size_t>> succs(n);
  for (auto& bptr : cfg.blocks) {
    BasicBlock* b = bptr.get();
    if (b->instructions.empty()) continue;
    size_t m = b->instructions.size();
    for (size_t i = 0; i < m; ++i) {
      size_t curIdx = idx[b->instructions[i]];
      if (i + 1 < m) {
        succs[curIdx].push_back(idx[b->instructions[i + 1]]);
      } else {
        for (auto* sb : b->succs) {
          auto it = blockFirst.find(sb);
          if (it != blockFirst.end()) succs[curIdx].push_back(it->second);
        }
      }
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
      std::unordered_set<int> use;
      int def = -1;
      collectUses(res.order[i], use);
      isDef(res.order[i], def);

      std::unordered_set<int> newOut;
      for (size_t s : succs[i]) {
        newOut.insert(res.liveIn[s].begin(), res.liveIn[s].end());
      }

      std::unordered_set<int> newIn = use;
      for (int v : newOut) {
        if (v != def) newIn.insert(v);
      }

      if (newIn != res.liveIn[i] || newOut != res.liveOut[i]) {
        res.liveIn[i] = std::move(newIn);
        res.liveOut[i] = std::move(newOut);
        changed = true;
      }
    }
  }

  return res;
}

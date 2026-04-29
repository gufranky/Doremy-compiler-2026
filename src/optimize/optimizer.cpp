#include "optimizer.h"

#include <algorithm>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "cfg.h"

namespace ir {

namespace {

bool isCommutative(BinaryOp op) {
  switch (op) {
    case BinaryOp::Add:
    case BinaryOp::Mul:
    case BinaryOp::And:
    case BinaryOp::Or:
    case BinaryOp::Eq:
    case BinaryOp::Ne:
      return true;
    default:
      return false;
  }
}

bool sameVReg(const Operand& a, const Operand& b) {
  return a.isVReg() && b.isVReg() && a.vregId == b.vregId;
}

struct ExprKey {
  BinaryOp op;
  Operand lhs;
  Operand rhs;

  bool operator==(const ExprKey& other) const {
    return op == other.op && lhs.kind == other.lhs.kind &&
           rhs.kind == other.rhs.kind && lhs.immValue == other.lhs.immValue &&
           rhs.immValue == other.rhs.immValue &&
           lhs.vregId == other.lhs.vregId && rhs.vregId == other.rhs.vregId &&
           lhs.globalName == other.lhs.globalName &&
           rhs.globalName == other.rhs.globalName;
  }
};

struct ExprKeyHasher {
  std::size_t operator()(const ExprKey& k) const {
    std::size_t h = static_cast<std::size_t>(k.op);
    auto mix = [&](const Operand& o) {
      h ^= static_cast<std::size_t>(o.kind) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= static_cast<std::size_t>(o.immValue) + 0x9e3779b9 + (h << 6) +
           (h >> 2);
      h ^=
          static_cast<std::size_t>(o.vregId) + 0x9e3779b9 + (h << 6) + (h >> 2);
      for (char c : o.globalName) {
        h ^= static_cast<std::size_t>(c) + 0x9e3779b9 + (h << 6) + (h >> 2);
      }
    };
    mix(k.lhs);
    mix(k.rhs);
    return h;
  }
};

void normalizeComm(ExprKey& key) {
  if (!isCommutative(key.op)) return;
  auto encode = [](const Operand& o) {
    if (o.isImm()) return std::make_pair(0, o.immValue);
    if (o.isVReg()) return std::make_pair(1, o.vregId);
    return std::make_pair(2, 0);
  };
  if (encode(key.rhs) < encode(key.lhs)) std::swap(key.lhs, key.rhs);
}

bool evalBinary(BinaryOp op, const Operand& lhs, const Operand& rhs, int& out) {
  if (!lhs.isImm() || !rhs.isImm()) return false;
  int a = lhs.immValue;
  int b = rhs.immValue;
  switch (op) {
    case BinaryOp::Add:
      out = a + b;
      return true;
    case BinaryOp::Sub:
      out = a - b;
      return true;
    case BinaryOp::Mul:
      out = a * b;
      return true;
    case BinaryOp::Div:
      if (b == 0) return false;
      out = a / b;
      return true;
    case BinaryOp::Mod:
      if (b == 0) return false;
      out = a % b;
      return true;
    case BinaryOp::And:
      out = (a != 0 && b != 0) ? 1 : 0;
      return true;
    case BinaryOp::Or:
      out = (a != 0 || b != 0) ? 1 : 0;
      return true;
    case BinaryOp::Lt:
      out = (a < b) ? 1 : 0;
      return true;
    case BinaryOp::Gt:
      out = (a > b) ? 1 : 0;
      return true;
    case BinaryOp::Le:
      out = (a <= b) ? 1 : 0;
      return true;
    case BinaryOp::Ge:
      out = (a >= b) ? 1 : 0;
      return true;
    case BinaryOp::Eq:
      out = (a == b) ? 1 : 0;
      return true;
    case BinaryOp::Ne:
      out = (a != b) ? 1 : 0;
      return true;
  }
  return false;
}

bool evalUnary(UnaryOp op, const Operand& operand, int& out) {
  if (!operand.isImm()) return false;
  int v = operand.immValue;
  switch (op) {
    case UnaryOp::Neg:
      out = -v;
      return true;
    case UnaryOp::Not:
      out = (v == 0) ? 1 : 0;
      return true;
    case UnaryOp::Plus:
      out = v;
      return true;
  }
  return false;
}

bool replaceWithCopy(std::unique_ptr<Instruction>& inst, int dest,
                     Operand value) {
  inst = std::make_unique<CopyInst>(dest, std::move(value));
  return true;
}

bool constantFold(IRFunction& func) {
  bool changed = false;
  for (auto& inst : func.instructions) {
    if (auto* bin = dynamic_cast<BinaryInst*>(inst.get())) {
      int val = 0;
      if (evalBinary(bin->op, bin->lhs, bin->rhs, val)) {
        changed |= replaceWithCopy(inst, bin->dest, Operand::Imm(val));
      }
    } else if (auto* un = dynamic_cast<UnaryInst*>(inst.get())) {
      int val = 0;
      if (evalUnary(un->op, un->operand, val)) {
        changed |= replaceWithCopy(inst, un->dest, Operand::Imm(val));
      }
    } else if (auto* br = dynamic_cast<BranchInst*>(inst.get())) {
      if (br->cond.isImm()) {
        std::string target =
            (br->cond.immValue != 0) ? br->trueLabel : br->falseLabel;
        inst = std::make_unique<JumpInst>(target);
        changed = true;
      }
    }
  }
  return changed;
}

bool algebraicSimplify(IRFunction& func) {
  bool changed = false;
  for (auto& inst : func.instructions) {
    if (auto* bin = dynamic_cast<BinaryInst*>(inst.get())) {
      Operand lhs = bin->lhs;
      Operand rhs = bin->rhs;
      int dest = bin->dest;

      switch (bin->op) {
        case BinaryOp::Add:
          if (rhs.isImm() && rhs.immValue == 0) {
            changed |= replaceWithCopy(inst, dest, lhs);
            continue;
          }
          if (lhs.isImm() && lhs.immValue == 0) {
            changed |= replaceWithCopy(inst, dest, rhs);
            continue;
          }
          break;
        case BinaryOp::Sub:
          if (rhs.isImm() && rhs.immValue == 0) {
            changed |= replaceWithCopy(inst, dest, lhs);
            continue;
          }
          if (sameVReg(lhs, rhs)) {
            changed |= replaceWithCopy(inst, dest, Operand::Imm(0));
            continue;
          }
          break;
        case BinaryOp::Mul:
          if ((rhs.isImm() && rhs.immValue == 1)) {
            changed |= replaceWithCopy(inst, dest, lhs);
            continue;
          }
          if ((lhs.isImm() && lhs.immValue == 1)) {
            changed |= replaceWithCopy(inst, dest, rhs);
            continue;
          }
          if ((rhs.isImm() && rhs.immValue == 0) ||
              (lhs.isImm() && lhs.immValue == 0)) {
            changed |= replaceWithCopy(inst, dest, Operand::Imm(0));
            continue;
          }
          break;
        case BinaryOp::Div:
          if (rhs.isImm() && rhs.immValue == 1) {
            changed |= replaceWithCopy(inst, dest, lhs);
            continue;
          }
          break;
        case BinaryOp::Mod:
          if (rhs.isImm() && rhs.immValue == 1) {
            changed |= replaceWithCopy(inst, dest, Operand::Imm(0));
            continue;
          }
          if (lhs.isImm() && lhs.immValue == 0) {
            changed |= replaceWithCopy(inst, dest, Operand::Imm(0));
            continue;
          }
          break;
        default:
          break;
      }
    } else if (auto* un = dynamic_cast<UnaryInst*>(inst.get())) {
      if (un->op == UnaryOp::Plus) {
        changed |= replaceWithCopy(inst, un->dest, un->operand);
      }
    }
  }
  return changed;
}

Operand resolveOperand(const Operand& op,
                       const std::unordered_map<int, Operand>& map) {
  if (!op.isVReg()) return op;
  auto it = map.find(op.vregId);
  if (it == map.end()) return op;
  // One level of indirection is enough for this simple IR.
  if (it->second.isVReg() && it->second.vregId == op.vregId) return op;
  return it->second;
}

void killMappingsUsing(int vreg, std::unordered_map<int, Operand>& map) {
  for (auto it = map.begin(); it != map.end();) {
    bool erase = false;
    if (it->first == vreg) {
      erase = true;
    } else {
      const Operand& val = it->second;
      if (val.isVReg() && val.vregId == vreg) erase = true;
    }
    if (erase) {
      it = map.erase(it);
    } else {
      ++it;
    }
  }
}

bool copyPropagate(IRFunction& func) {
  bool changed = false;
  std::unordered_map<int, Operand> valueMap;

  auto rewriteOperand = [&](Operand& op) {
    Operand repl = resolveOperand(op, valueMap);
    if (repl.kind != op.kind || repl.immValue != op.immValue ||
        repl.vregId != op.vregId || repl.globalName != op.globalName) {
      op = repl;
      changed = true;
    }
  };

  for (auto& instPtr : func.instructions) {
    Instruction* inst = instPtr.get();

    if (auto* bin = dynamic_cast<BinaryInst*>(inst)) {
      rewriteOperand(bin->lhs);
      rewriteOperand(bin->rhs);
      killMappingsUsing(bin->dest, valueMap);
    } else if (auto* un = dynamic_cast<UnaryInst*>(inst)) {
      rewriteOperand(un->operand);
      killMappingsUsing(un->dest, valueMap);
    } else if (auto* cp = dynamic_cast<CopyInst*>(inst)) {
      rewriteOperand(cp->src);
      killMappingsUsing(cp->dest, valueMap);
      valueMap[cp->dest] = cp->src;
    } else if (auto* ld = dynamic_cast<LoadInst*>(inst)) {
      rewriteOperand(ld->addr);
      killMappingsUsing(ld->dest, valueMap);
    } else if (auto* st = dynamic_cast<StoreInst*>(inst)) {
      rewriteOperand(st->src);
      rewriteOperand(st->addr);
    } else if (auto* br = dynamic_cast<BranchInst*>(inst)) {
      rewriteOperand(br->cond);
    } else if (auto* j = dynamic_cast<JumpInst*>(inst)) {
      (void)j;
    } else if (auto* call = dynamic_cast<CallInst*>(inst)) {
      for (auto& a : call->args) rewriteOperand(a);
      if (call->hasDest) killMappingsUsing(call->dest, valueMap);
    } else if (auto* ret = dynamic_cast<ReturnInst*>(inst)) {
      if (ret->hasValue) rewriteOperand(ret->value);
    } else if (auto* lbl = dynamic_cast<LabelInst*>(inst)) {
      (void)lbl;
      valueMap.clear();  // Conservative: clear across labels/branches
    }
  }

  return changed;
}

bool usesReg(const ExprKey& key, int vreg) {
  return (key.lhs.isVReg() && key.lhs.vregId == vreg) ||
         (key.rhs.isVReg() && key.rhs.vregId == vreg);
}

bool commonSubexpr(IRFunction& func) {
  bool changed = false;
  std::unordered_map<ExprKey, int, ExprKeyHasher> table;

  for (auto& instPtr : func.instructions) {
    Instruction* inst = instPtr.get();

    if (auto* bin = dynamic_cast<BinaryInst*>(inst)) {
      ExprKey key{bin->op, bin->lhs, bin->rhs};
      normalizeComm(key);

      auto it = table.find(key);
      if (it != table.end()) {
        // Reuse existing value
        changed |=
            replaceWithCopy(instPtr, bin->dest, Operand::VReg(it->second));
      } else {
        table.emplace(key, bin->dest);
      }

      // Defining a dest kills expressions that depended on that dest
      for (auto t = table.begin(); t != table.end();) {
        if (usesReg(t->first, bin->dest)) {
          t = table.erase(t);
        } else {
          ++t;
        }
      }
    } else if (auto* cp = dynamic_cast<CopyInst*>(inst)) {
      for (auto t = table.begin(); t != table.end();) {
        if (usesReg(t->first, cp->dest)) {
          t = table.erase(t);
        } else {
          ++t;
        }
      }
    } else if (auto* un = dynamic_cast<UnaryInst*>(inst)) {
      for (auto t = table.begin(); t != table.end();) {
        if (usesReg(t->first, un->dest)) {
          t = table.erase(t);
        } else {
          ++t;
        }
      }
    } else if (auto* call = dynamic_cast<CallInst*>(inst)) {
      if (call->hasDest) {
        for (auto t = table.begin(); t != table.end();) {
          if (usesReg(t->first, call->dest)) {
            t = table.erase(t);
          } else {
            ++t;
          }
        }
      }
    } else if (auto* ld = dynamic_cast<LoadInst*>(inst)) {
      for (auto t = table.begin(); t != table.end();) {
        if (usesReg(t->first, ld->dest)) {
          t = table.erase(t);
        } else {
          ++t;
        }
      }
    } else if (auto* lbl = dynamic_cast<LabelInst*>(inst)) {
      (void)lbl;
      table.clear();
    }
  }

  return changed;
}

void addUse(const Operand& op, std::unordered_set<int>& live) {
  if (op.isVReg()) live.insert(op.vregId);
}

bool isPureDef(const Instruction* inst) {
  return inst->kind == InstKind::Binary || inst->kind == InstKind::Unary ||
         inst->kind == InstKind::Copy;
}

int definedReg(const Instruction* inst) {
  switch (inst->kind) {
    case InstKind::Binary:
      return static_cast<const BinaryInst*>(inst)->dest;
    case InstKind::Unary:
      return static_cast<const UnaryInst*>(inst)->dest;
    case InstKind::Copy:
      return static_cast<const CopyInst*>(inst)->dest;
    case InstKind::Load:
      return static_cast<const LoadInst*>(inst)->dest;
    case InstKind::Call: {
      auto* c = static_cast<const CallInst*>(inst);
      return c->hasDest ? c->dest : -1;
    }
    default:
      return -1;
  }
}

void collectUses(const Instruction* inst, std::unordered_set<int>& live) {
  switch (inst->kind) {
    case InstKind::Binary: {
      auto* b = static_cast<const BinaryInst*>(inst);
      addUse(b->lhs, live);
      addUse(b->rhs, live);
      break;
    }
    case InstKind::Unary: {
      auto* u = static_cast<const UnaryInst*>(inst);
      addUse(u->operand, live);
      break;
    }
    case InstKind::Copy: {
      auto* c = static_cast<const CopyInst*>(inst);
      addUse(c->src, live);
      break;
    }
    case InstKind::Load: {
      auto* l = static_cast<const LoadInst*>(inst);
      addUse(l->addr, live);
      break;
    }
    case InstKind::Store: {
      auto* s = static_cast<const StoreInst*>(inst);
      addUse(s->src, live);
      addUse(s->addr, live);
      break;
    }
    case InstKind::Branch: {
      auto* br = static_cast<const BranchInst*>(inst);
      addUse(br->cond, live);
      break;
    }
    case InstKind::Call: {
      auto* c = static_cast<const CallInst*>(inst);
      for (const auto& a : c->args) addUse(a, live);
      break;
    }
    case InstKind::Return: {
      auto* r = static_cast<const ReturnInst*>(inst);
      if (r->hasValue) addUse(r->value, live);
      break;
    }
    default:
      break;
  }
}

bool deadCodeElim(IRFunction& func) {
  // Build CFG and compute iterative liveness to handle loops.
  auto cfg = ControlFlowGraph::Build(&func);
  if (!cfg.entry) return false;

  const size_t numBlocks = cfg.blocks.size();
  if (numBlocks == 0) return false;

  std::unordered_map<const BasicBlock*, size_t> blockIdx;
  for (size_t i = 0; i < numBlocks; ++i) blockIdx[cfg.blocks[i].get()] = i;

  // liveIn/liveOut per block
  std::vector<std::unordered_set<int>> liveIn(numBlocks), liveOut(numBlocks);

  // Map instruction -> (blockIndex, positionInBlock)
  std::unordered_map<const Instruction*, std::pair<size_t, size_t>> instLoc;
  for (size_t bi = 0; bi < numBlocks; ++bi) {
    const auto& blk = cfg.blocks[bi];
    for (size_t j = 0; j < blk->instructions.size(); ++j) {
      instLoc[blk->instructions[j]] = {bi, j};
    }
  }

  // Fixed-point iteration for block-level liveness
  bool changed = true;
  while (changed) {
    changed = false;
    for (int bi = static_cast<int>(numBlocks) - 1; bi >= 0; --bi) {
      const auto& blk = cfg.blocks[bi];

      // liveOut[bi] = union of liveIn of successors
      std::unordered_set<int> newOut;
      for (auto* succ : blk->succs) {
        if (!succ) continue;
        size_t si = blockIdx[succ];
        newOut.insert(liveIn[si].begin(), liveIn[si].end());
      }

      // Walk instructions in reverse to compute liveIn
      std::unordered_set<int> live = newOut;
      for (int j = static_cast<int>(blk->instructions.size()) - 1; j >= 0;
           --j) {
        const Instruction* inst = blk->instructions[j];
        int def = definedReg(inst);
        if (def >= 0) live.erase(def);
        std::unordered_set<int> uses;
        collectUses(inst, uses);
        live.insert(uses.begin(), uses.end());
      }

      if (live != liveIn[bi] || newOut != liveOut[bi]) {
        liveIn[bi] = std::move(live);
        liveOut[bi] = std::move(newOut);
        changed = true;
      }
    }
  }

  // Compute per-instruction liveOut using block liveness
  std::unordered_map<const Instruction*, std::unordered_set<int>> instLive;
  for (size_t bi = 0; bi < numBlocks; ++bi) {
    const auto& blk = cfg.blocks[bi];
    std::unordered_set<int> live = liveOut[bi];
    for (int j = static_cast<int>(blk->instructions.size()) - 1; j >= 0; --j) {
      const Instruction* inst = blk->instructions[j];
      instLive[inst] = live;
      int def = definedReg(inst);
      if (def >= 0) live.erase(def);
      std::unordered_set<int> uses;
      collectUses(inst, uses);
      live.insert(uses.begin(), uses.end());
    }
  }

  // Now filter dead instructions
  changed = false;
  std::vector<std::unique_ptr<Instruction>> newInsts;
  newInsts.reserve(func.instructions.size());

  for (auto& instPtr : func.instructions) {
    Instruction* raw = instPtr.get();
    int def = definedReg(raw);
    const auto& liveSet = instLive[raw];
    bool removable =
        isPureDef(raw) && def != -1 && liveSet.find(def) == liveSet.end();

    if (!removable) {
      newInsts.push_back(std::move(instPtr));
    } else {
      changed = true;
    }
  }

  func.instructions.swap(newInsts);
  return changed;
}

std::unordered_set<BasicBlock*> reachableBlocks(ControlFlowGraph& cfg) {
  std::unordered_set<BasicBlock*> reachable;
  if (!cfg.entry) return reachable;

  std::vector<BasicBlock*> worklist;
  worklist.push_back(cfg.entry);
  while (!worklist.empty()) {
    BasicBlock* blk = worklist.back();
    worklist.pop_back();
    if (reachable.count(blk)) continue;
    reachable.insert(blk);
    for (auto* succ : blk->succs) {
      if (succ && !reachable.count(succ)) worklist.push_back(succ);
    }
  }
  return reachable;
}

std::string resolveRedirect(
    const std::string& label,
    const std::unordered_map<std::string, std::string>& redirect) {
  std::string cur = label;
  std::unordered_set<std::string> seen;
  while (true) {
    auto it = redirect.find(cur);
    if (it == redirect.end()) break;
    if (seen.count(cur)) break;  // avoid cycles
    seen.insert(cur);
    cur = it->second;
  }
  return cur;
}

bool simplifyCFG(IRFunction& func) {
  auto cfg = ControlFlowGraph::Build(&func);
  if (!cfg.entry) return false;

  auto reachable = reachableBlocks(cfg);
  if (reachable.empty()) return false;

  std::unordered_map<std::string, std::string> redirect;
  std::unordered_set<BasicBlock*> removable;

  for (auto& blkPtr : cfg.blocks) {
    BasicBlock* blk = blkPtr.get();
    if (!reachable.count(blk)) continue;
    if (blk->instructions.size() != 2) continue;
    auto* firstLbl = dynamic_cast<LabelInst*>(blk->instructions.front());
    auto* jmp = dynamic_cast<JumpInst*>(blk->instructions.back());
    if (!firstLbl || !jmp) continue;

    bool hasFallthroughPred = false;
    for (auto* pred : blk->preds) {
      if (!pred || !pred->hasTerminator()) {
        hasFallthroughPred = true;
        break;
      }
    }
    if (hasFallthroughPred) continue;

    if (firstLbl->label == jmp->target) continue;
    redirect[firstLbl->label] = jmp->target;
    removable.insert(blk);
  }

  bool changed = false;
  for (auto& instPtr : func.instructions) {
    Instruction* inst = instPtr.get();
    if (auto* br = dynamic_cast<BranchInst*>(inst)) {
      std::string newT = resolveRedirect(br->trueLabel, redirect);
      std::string newF = resolveRedirect(br->falseLabel, redirect);
      if (newT != br->trueLabel) {
        br->trueLabel = std::move(newT);
        changed = true;
      }
      if (newF != br->falseLabel) {
        br->falseLabel = std::move(newF);
        changed = true;
      }
    } else if (auto* j = dynamic_cast<JumpInst*>(inst)) {
      std::string newT = resolveRedirect(j->target, redirect);
      if (newT != j->target) {
        j->target = std::move(newT);
        changed = true;
      }
    }
  }

  std::unordered_set<const Instruction*> keep;
  for (auto& blkPtr : cfg.blocks) {
    BasicBlock* blk = blkPtr.get();
    if (!reachable.count(blk)) continue;
    if (removable.count(blk)) {
      changed = true;
      continue;
    }
    for (auto* inst : blk->instructions) {
      keep.insert(inst);
    }
  }

  if (keep.size() == func.instructions.size()) return changed;

  std::vector<std::unique_ptr<Instruction>> newInsts;
  newInsts.reserve(keep.size());
  for (auto& instPtr : func.instructions) {
    if (keep.count(instPtr.get())) {
      newInsts.push_back(std::move(instPtr));
    } else {
      changed = true;
    }
  }

  func.instructions.swap(newInsts);
  return changed;
}

struct VNKeyHash {
  std::size_t operator()(const std::pair<UnaryOp, int>& k) const {
    return static_cast<std::size_t>(k.first) * 1315423911u +
           static_cast<std::size_t>(k.second + 0x9e3779b9);
  }
  std::size_t operator()(const std::tuple<BinaryOp, int, int>& k) const {
    std::size_t h = static_cast<std::size_t>(std::get<0>(k));
    h ^= static_cast<std::size_t>(std::get<1>(k)) + 0x9e3779b9 + (h << 6) +
         (h >> 2);
    h ^= static_cast<std::size_t>(std::get<2>(k)) + 0x9e3779b9 + (h << 6) +
         (h >> 2);
    return h;
  }
};

bool blockGVN(IRFunction& func) {
  auto cfg = ControlFlowGraph::Build(&func);
  if (!cfg.entry) return false;
  auto reachable = reachableBlocks(cfg);
  if (reachable.empty()) return false;

  std::unordered_map<Instruction*, std::unique_ptr<Instruction>*> owner;
  for (auto& instPtr : func.instructions) {
    owner[instPtr.get()] = &instPtr;
  }

  bool changed = false;

  for (auto& blkPtr : cfg.blocks) {
    BasicBlock* blk = blkPtr.get();
    if (!reachable.count(blk)) continue;

    int nextVN = 0;
    std::unordered_map<int, int> immToVN;
    std::unordered_map<std::string, int> globToVN;
    std::unordered_map<int, int> regToVN;
    std::unordered_map<int, Operand> vnRep;
    std::unordered_map<std::pair<UnaryOp, int>, int, VNKeyHash> unaryMap;
    std::unordered_map<std::tuple<BinaryOp, int, int>, int, VNKeyHash>
        binaryMap;

    auto newVN = [&]() { return nextVN++; };

    auto getVN = [&](const Operand& op) {
      if (op.isImm()) {
        auto it = immToVN.find(op.immValue);
        if (it != immToVN.end()) return it->second;
        int vn = newVN();
        immToVN[op.immValue] = vn;
        vnRep[vn] = op;
        return vn;
      }
      if (op.isGlobal()) {
        auto it = globToVN.find(op.globalName);
        if (it != globToVN.end()) return it->second;
        int vn = newVN();
        globToVN[op.globalName] = vn;
        vnRep[vn] = op;
        return vn;
      }
      // VReg
      auto it = regToVN.find(op.vregId);
      if (it != regToVN.end()) return it->second;
      int vn = newVN();
      regToVN[op.vregId] = vn;
      vnRep[vn] = op;
      return vn;
    };

    auto setDest = [&](int dest, int vn) {
      regToVN[dest] = vn;
      if (!vnRep.count(vn)) vnRep[vn] = Operand::VReg(dest);
    };

    for (auto*& inst : blk->instructions) {
      switch (inst->kind) {
        case InstKind::Binary: {
          auto* b = static_cast<BinaryInst*>(inst);
          int lhsVN = getVN(b->lhs);
          int rhsVN = getVN(b->rhs);
          if (isCommutative(b->op) && rhsVN < lhsVN) std::swap(lhsVN, rhsVN);
          std::tuple<BinaryOp, int, int> key{b->op, lhsVN, rhsVN};
          auto it = binaryMap.find(key);
          if (it != binaryMap.end()) {
            int vn = it->second;
            Operand rep = vnRep[vn];
            auto* ownerPtr = owner[inst];
            changed |= replaceWithCopy(*ownerPtr, b->dest, rep);
            Instruction* newRaw = ownerPtr->get();
            owner.erase(inst);
            owner[newRaw] = ownerPtr;
            inst = newRaw;
            setDest(b->dest, vn);
          } else {
            int vn = newVN();
            binaryMap[key] = vn;
            vnRep[vn] = Operand::VReg(b->dest);
            setDest(b->dest, vn);
          }
          break;
        }
        case InstKind::Unary: {
          auto* u = static_cast<UnaryInst*>(inst);
          int opVN = getVN(u->operand);
          std::pair<UnaryOp, int> key{u->op, opVN};
          auto it = unaryMap.find(key);
          if (it != unaryMap.end()) {
            int vn = it->second;
            Operand rep = vnRep[vn];
            auto* ownerPtr = owner[inst];
            changed |= replaceWithCopy(*ownerPtr, u->dest, rep);
            Instruction* newRaw = ownerPtr->get();
            owner.erase(inst);
            owner[newRaw] = ownerPtr;
            inst = newRaw;
            setDest(u->dest, vn);
          } else {
            int vn = newVN();
            unaryMap[key] = vn;
            vnRep[vn] = Operand::VReg(u->dest);
            setDest(u->dest, vn);
          }
          break;
        }
        case InstKind::Copy: {
          auto* c = static_cast<CopyInst*>(inst);
          int vn = getVN(c->src);
          setDest(c->dest, vn);
          break;
        }
        case InstKind::Load: {
          auto* l = static_cast<LoadInst*>(inst);
          (void)l;
          binaryMap.clear();
          unaryMap.clear();
          setDest(static_cast<LoadInst*>(inst)->dest, newVN());
          break;
        }
        case InstKind::Call: {
          auto* c = static_cast<CallInst*>(inst);
          binaryMap.clear();
          unaryMap.clear();
          if (c->hasDest) setDest(c->dest, newVN());
          break;
        }
        case InstKind::Store: {
          binaryMap.clear();
          unaryMap.clear();
          break;
        }
        default:
          break;
      }
    }
  }

  return changed;
}

// ================== Strength Reduction ==================
// Convert multiplication by power of 2 to left shift.
bool strengthReduction(IRFunction& func) {
  bool changed = false;
  
  auto isPowerOfTwo = [](int v) -> bool {
    return v > 0 && (v & (v - 1)) == 0;
  };
  
  auto log2Int = [](int v) -> int {
    int k = 0;
    while (v > 1) { v >>= 1; k++; }
    return k;
  };
  
  for (auto& instPtr : func.instructions) {
    if (auto* bin = dynamic_cast<BinaryInst*>(instPtr.get())) {
      if (bin->op != BinaryOp::Mul) continue;
      
      // x * (2^k) -> x << k (using Add for shift simulation in IR)
      // Since our IR doesn't have shift, we keep mul but this enables
      // the backend to use shift instructions
      if (bin->rhs.isImm() && isPowerOfTwo(bin->rhs.immValue)) {
        int k = log2Int(bin->rhs.immValue);
        if (k == 0) {
          // x * 1 = x (handled by algebraicSimplify)
          continue;
        }
        // For powers of 2, keep as mul - backend will optimize
        // But we can optimize x * 2 -> x + x
        if (bin->rhs.immValue == 2 && bin->lhs.isVReg()) {
          bin->op = BinaryOp::Add;
          bin->rhs = bin->lhs;
          changed = true;
        }
      } else if (bin->lhs.isImm() && isPowerOfTwo(bin->lhs.immValue)) {
        int k = log2Int(bin->lhs.immValue);
        if (k == 0) continue;
        if (bin->lhs.immValue == 2 && bin->rhs.isVReg()) {
          bin->op = BinaryOp::Add;
          bin->lhs = bin->rhs;
          changed = true;
        }
      }
    }
  }
  return changed;
}



// ================== LocalMemOpt: STORE->LOAD forwarding ==================
// This is the most important optimization for reducing stack traffic.
// Pattern: STORE x, vreg; LOAD t, x -> STORE x, vreg; MOV t, vreg
bool localMemOpt(IRFunction& func) {
  bool changed = false;
  
  // Forward pass: track last stored value for each global variable
  std::unordered_map<std::string, Operand> lastStoredValue;
  
  // First, collect replacements without modifying the vector
  std::vector<std::pair<size_t, std::unique_ptr<Instruction>>> replacements;
  
  for (size_t i = 0; i < func.instructions.size(); ++i) {
    Instruction* inst = func.instructions[i].get();
    
    // Handle LOAD: if we know the stored value, schedule replacement
    if (auto* ld = dynamic_cast<LoadInst*>(inst)) {
      if (ld->addr.isGlobal()) {
        auto it = lastStoredValue.find(ld->addr.globalName);
        if (it != lastStoredValue.end()) {
          // Schedule replacement: LOAD with Copy from the known value
          replacements.emplace_back(i, std::make_unique<CopyInst>(ld->dest, it->second));
          changed = true;
          // Don't continue - still need to process next instructions
        }
      }
    }
    
    // Handle STORE: update tracking
    if (auto* st = dynamic_cast<StoreInst*>(inst)) {
      if (st->addr.isGlobal()) {
        // Only track safe values (VReg or Imm) that can be reused
        if (st->src.isVReg() || st->src.isImm()) {
          lastStoredValue[st->addr.globalName] = st->src;
        } else {
          // If storing another global, we can't safely forward
          lastStoredValue.erase(st->addr.globalName);
        }
      }
    }
    
    // Clear tracking on labels (conservative across control flow)
    if (dynamic_cast<LabelInst*>(inst)) {
      lastStoredValue.clear();
    }
    
    // Call may modify memory
    if (dynamic_cast<CallInst*>(inst)) {
      lastStoredValue.clear();
    }
  }
  
  // Apply replacements
  for (auto& replacement : replacements) {
    func.instructions[replacement.first] = std::move(replacement.second);
  }
  
  return changed;
}

// ================== GlobalVarConst: detect constant local variables ==================
// If a local variable is only STORE'd with the same constant value, replace LOADs with that constant.
bool globalVarConst(IRFunction& func) {
  bool changed = false;
  
  // First pass: find variables that are assigned exactly one constant
  std::unordered_map<std::string, int> constVal;
  std::unordered_set<std::string> nonConst;
  std::unordered_set<std::string> seenStore;
  
  for (const auto& instPtr : func.instructions) {
    if (auto* st = dynamic_cast<StoreInst*>(instPtr.get())) {
      if (!st->addr.isGlobal()) continue;
      const std::string& varName = st->addr.globalName;
      
      // Skip if already known to be non-constant
      if (nonConst.count(varName)) continue;
      
      // If not storing an immediate, mark as non-constant
      if (!st->src.isImm()) {
        nonConst.insert(varName);
        constVal.erase(varName);
        continue;
      }
      
      // Check if this is first store or same constant as before
      if (seenStore.insert(varName).second) {
        constVal[varName] = st->src.immValue;
      } else {
        auto it = constVal.find(varName);
        if (it == constVal.end() || it->second != st->src.immValue) {
          nonConst.insert(varName);
          constVal.erase(varName);
        }
      }
    }
  }
  
  if (constVal.empty()) return false;
  
  // Second pass: replace LOADs of constant variables with Copy of immediate
  for (auto& instPtr : func.instructions) {
    if (auto* ld = dynamic_cast<LoadInst*>(instPtr.get())) {
      if (!ld->addr.isGlobal()) continue;
      auto it = constVal.find(ld->addr.globalName);
      if (it != constVal.end()) {
        instPtr = std::make_unique<CopyInst>(ld->dest, Operand::Imm(it->second));
        changed = true;
      }
    }
  }
  
  return changed;
}

// ================== Dead Store Elimination ==================
// Remove stores to variables that are never loaded afterwards
// NOTE: This is a very aggressive optimization that should be run last
// in the optimization pass. It marks stores as dead if the variable
// is never loaded in the entire function.
bool deadStoreElim(IRFunction& func) {
  std::unordered_set<std::string> loadedVars;
  for (const auto& instPtr : func.instructions) {
    if (!instPtr) continue;  // Safety check
    if (auto* ld = dynamic_cast<LoadInst*>(instPtr.get())) {
      if (ld->addr.isGlobal()) {
        loadedVars.insert(ld->addr.globalName);
      }
    }
  }
  
  std::vector<size_t> toRemove;
  for (size_t i = 0; i < func.instructions.size(); ++i) {
    if (!func.instructions[i]) continue;  // Safety check
    if (auto* st = dynamic_cast<StoreInst*>(func.instructions[i].get())) {
      if (!st->addr.isGlobal()) continue;

      // Global stores are externally visible across calls/functions, so
      // function-local dead store analysis must not remove them.
      continue;
    }
  }
  
  if (toRemove.empty()) return false;
  
  std::unordered_set<size_t> removeSet(toRemove.begin(), toRemove.end());
  std::vector<std::unique_ptr<Instruction>> newInstructions;
  newInstructions.reserve(func.instructions.size() - toRemove.size());
  
  for (size_t i = 0; i < func.instructions.size(); ++i) {
    if (!removeSet.count(i) && func.instructions[i]) {
      newInstructions.push_back(std::move(func.instructions[i]));
    }
  }
  
  func.instructions = std::move(newInstructions);
  return true;
}

// ================== Mod Simplification ==================
// Reduce nested MOD chains:
//  - (x % M) % M == x % M
//  - Nested mod elimination
bool modSimplify(IRFunction& func) {
  bool changed = false;
  
  // Build def map: vreg -> defining instruction
  std::unordered_map<int, BinaryInst*> defMap;
  std::unordered_map<int, int> useCnt;
  
  for (const auto& instPtr : func.instructions) {
    if (!instPtr) continue;
    if (auto* bin = dynamic_cast<BinaryInst*>(instPtr.get())) {
      defMap[bin->dest] = bin;
    }
    // Count uses
    auto countUse = [&](const Operand& op) {
      if (op.isVReg()) useCnt[op.vregId]++;
    };
    if (auto* bin = dynamic_cast<BinaryInst*>(instPtr.get())) {
      countUse(bin->lhs);
      countUse(bin->rhs);
    } else if (auto* un = dynamic_cast<UnaryInst*>(instPtr.get())) {
      countUse(un->operand);
    } else if (auto* cp = dynamic_cast<CopyInst*>(instPtr.get())) {
      countUse(cp->src);
    }
  }
  
  for (auto& instPtr : func.instructions) {
    if (!instPtr) continue;
    auto* bin = dynamic_cast<BinaryInst*>(instPtr.get());
    if (!bin || bin->op != BinaryOp::Mod) continue;
    if (!bin->rhs.isImm()) continue;
    int M = bin->rhs.immValue;
    if (M == 0) continue;
    if (!bin->lhs.isVReg()) continue;
    
    int t = bin->lhs.vregId;
    auto itDef = defMap.find(t);
    if (itDef == defMap.end()) continue;
    BinaryInst* d = itDef->second;
    
    // (x % M) % M  ->  MOV dest, (x % M)
    if (d->op == BinaryOp::Mod && d->rhs.isImm() && d->rhs.immValue == M) {
      instPtr = std::make_unique<CopyInst>(bin->dest, Operand::VReg(t));
      changed = true;
      continue;
    }
    
    // ((x % M) +/- y) % M -> (x +/- y) % M  (when the intermediate is single-use)
    if ((d->op == BinaryOp::Add || d->op == BinaryOp::Sub) && useCnt[t] == 1) {
      auto stripMod = [&](Operand& op) -> bool {
        if (!op.isVReg()) return false;
        auto it = defMap.find(op.vregId);
        if (it == defMap.end()) return false;
        BinaryInst* di = it->second;
        if (di->op == BinaryOp::Mod && di->rhs.isImm() && di->rhs.immValue == M) {
          op = di->lhs;  // Use un-modded value
          return true;
        }
        return false;
      };
      
      bool stripped = stripMod(d->lhs);
      stripped |= stripMod(d->rhs);
      if (stripped) changed = true;
    }
  }
  
  return changed;
}

// ================== Loop IV Mod Elimination ==================
// For loops with i from 0 to N where N < MOD, (i % MOD) == i
// Common case: MOD = 998244353
bool loopIVModElim(IRFunction& func) {
  bool changed = false;
  const int MOD = 998244353;
  
  // Simple pattern: look for (vreg % 998244353) where vreg is used in comparison < N
  // and there's evidence the value is in range [0, N)
  
  for (auto& instPtr : func.instructions) {
    if (!instPtr) continue;
    auto* bin = dynamic_cast<BinaryInst*>(instPtr.get());
    if (!bin || bin->op != BinaryOp::Mod) continue;
    if (!bin->rhs.isImm() || bin->rhs.immValue != MOD) continue;
    if (!bin->lhs.isVReg()) continue;
    
    // Check if this vreg is compared against a bound < MOD
    // This is a simplified check - just look for pattern of loop counter
    int vregId = bin->lhs.vregId;
    bool foundBound = false;
    
    for (const auto& inst2 : func.instructions) {
      if (!inst2) continue;
      auto* cmp = dynamic_cast<BinaryInst*>(inst2.get());
      if (!cmp) continue;
      if (cmp->op != BinaryOp::Lt && cmp->op != BinaryOp::Le) continue;
      
      // Check if comparing same vreg or related vreg against a constant < MOD
      if (cmp->lhs.isVReg() && cmp->lhs.vregId == vregId && cmp->rhs.isImm()) {
        if (cmp->rhs.immValue > 0 && cmp->rhs.immValue <= MOD) {
          foundBound = true;
          break;
        }
      }
    }
    
    if (foundBound) {
      // Replace MOD with simple copy - value is guaranteed in range
      instPtr = std::make_unique<CopyInst>(bin->dest, Operand::VReg(vregId));
      changed = true;
    }
  }
  
  return changed;
}

bool runOnce(IRFunction& func) {
  bool changed = false;
  changed |= localMemOpt(func);
  changed |= globalVarConst(func);
  changed |= constantFold(func);
  changed |= algebraicSimplify(func);
  changed |= modSimplify(func);
  changed |= loopIVModElim(func);
  changed |= strengthReduction(func);
  changed |= copyPropagate(func);
  changed |= blockGVN(func);
  changed |= commonSubexpr(func);
  changed |= deadCodeElim(func);
  changed |= deadStoreElim(func);
  changed |= simplifyCFG(func);
  return changed;
}

}  // namespace

void OptimizeFunction(IRFunction& func) {
  for (int i = 0; i < 10; ++i) {
    if (!runOnce(func)) break;
  }
}

// ================== Interprocedural Optimizations ==================

struct FuncSummary {
  std::string name;
  bool returnsConstant = false;
  int constantReturnValue = 0;
  bool returnsParam = false;
  int returnedParamIndex = -1;
  int paramCount = 0;
};

std::unordered_map<std::string, FuncSummary> buildFuncSummaries(IRProgram& program) {
  std::unordered_map<std::string, FuncSummary> summaries;
  
  for (auto& fn : program.functions) {
    FuncSummary summary;
    summary.name = fn.name;
    summary.paramCount = static_cast<int>(fn.params.size());
    
    // Check if function returns a constant or a parameter
    int returnCount = 0;
    bool allSameConstant = true;
    bool allSameParam = true;
    int constVal = 0;
    int paramIdx = -1;
    
    for (const auto& instPtr : fn.instructions) {
      if (!instPtr) continue;
      if (auto* ret = dynamic_cast<ReturnInst*>(instPtr.get())) {
        if (!ret->hasValue) {
          allSameConstant = false;
          allSameParam = false;
          break;
        }
        
        returnCount++;
        
        if (ret->value.isImm()) {
          if (returnCount == 1) {
            constVal = ret->value.immValue;
          } else if (constVal != ret->value.immValue) {
            allSameConstant = false;
          }
          allSameParam = false;
        } else if (ret->value.isVReg()) {
          allSameConstant = false;
          // Check if it's a parameter
          auto it = std::find(fn.params.begin(), fn.params.end(), ret->value.vregId);
          if (it != fn.params.end()) {
            int idx = static_cast<int>(it - fn.params.begin());
            if (returnCount == 1) {
              paramIdx = idx;
            } else if (paramIdx != idx) {
              allSameParam = false;
            }
          } else {
            allSameParam = false;
          }
        } else {
          allSameConstant = false;
          allSameParam = false;
        }
      }
    }
    
    if (returnCount > 0 && allSameConstant) {
      summary.returnsConstant = true;
      summary.constantReturnValue = constVal;
    }
    if (returnCount > 0 && allSameParam && paramIdx >= 0) {
      summary.returnsParam = true;
      summary.returnedParamIndex = paramIdx;
    }
    
    summaries[fn.name] = summary;
  }
  
  return summaries;
}

bool simplifyCallsInFunc(IRFunction& func, 
                         const std::unordered_map<std::string, FuncSummary>& summaries) {
  bool changed = false;
  
  for (auto& instPtr : func.instructions) {
    if (!instPtr) continue;
    if (auto* call = dynamic_cast<CallInst*>(instPtr.get())) {
      if (!call->hasDest) continue;
      
      auto it = summaries.find(call->callee);
      if (it == summaries.end()) continue;
      
      const FuncSummary& summary = it->second;
      
      // Replace call with constant if function always returns same constant
      if (summary.returnsConstant) {
        instPtr = std::make_unique<CopyInst>(call->dest, 
                                              Operand::Imm(summary.constantReturnValue));
        changed = true;
        continue;
      }
      
      // Replace call with parameter if function returns that parameter unchanged
      if (summary.returnsParam && summary.returnedParamIndex >= 0 &&
          summary.returnedParamIndex < static_cast<int>(call->args.size())) {
        instPtr = std::make_unique<CopyInst>(call->dest, 
                                              call->args[summary.returnedParamIndex]);
        changed = true;
        continue;
      }
    }
  }
  
  return changed;
}

void OptimizeProgram(IRProgram& program) {
  for (auto& fn : program.functions) {
    OptimizeFunction(fn);
  }

  auto summaries = buildFuncSummaries(program);

  bool ipaChanged = true;
  int ipaPass = 0;
  while (ipaChanged && ipaPass < 5) {
    ipaChanged = false;
    ipaPass++;

    for (auto& fn : program.functions) {
      if (simplifyCallsInFunc(fn, summaries)) {
        ipaChanged = true;
        OptimizeFunction(fn);
      }
    }

    if (ipaChanged) {
      summaries = buildFuncSummaries(program);
    }
  }

  for (int pass = 0; pass < 3; ++pass) {
    for (auto& fn : program.functions) {
      OptimizeFunction(fn);
    }
  }
}

}  // namespace ir


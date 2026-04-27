#include <algorithm>
#include <iostream>
#include <limits>
#include <queue>

#include "backend_regalloc.h"

namespace regalloc {

// InterferenceGraph methods
void InterferenceGraph::addNode(int vreg) {
  nodes.insert(vreg);
  if (adj.find(vreg) == adj.end()) {
    adj[vreg] = std::set<int>();
  }
}

void InterferenceGraph::addEdge(int u, int v) {
  if (u == v) return;  // No self-loops
  addNode(u);
  addNode(v);
  adj[u].insert(v);
  adj[v].insert(u);
}

int InterferenceGraph::degree(int node) const {
  auto it = adj.find(node);
  return (it != adj.end()) ? it->second.size() : 0;
}

std::set<int> InterferenceGraph::neighbors(int node) const {
  auto it = adj.find(node);
  return (it != adj.end()) ? it->second : std::set<int>();
}

void InterferenceGraph::removeNode(int node) {
  nodes.erase(node);
  // Remove edges to this node from all neighbors
  auto it = adj.find(node);
  if (it != adj.end()) {
    for (int neighbor : it->second) {
      adj[neighbor].erase(node);
    }
    adj.erase(node);
  }
}

// Build interference graph from liveness analysis
InterferenceGraph GraphColoringAllocator::buildInterferenceGraph(
    const ir::IRFunction& func, const LivenessResult& liveness) {
  InterferenceGraph graph;

  // All function parameters must interfere with each other because they are
  // all live at function entry and need distinct registers.
  for (size_t i = 0; i < func.params.size(); ++i) {
    graph.addNode(func.params[i]);
    for (size_t j = i + 1; j < func.params.size(); ++j) {
      graph.addEdge(func.params[i], func.params[j]);
    }
  }

  // Add all virtual registers appearing in operands/defs to ensure they get a
  // color even if not live with others.
  for (const auto* inst : liveness.order) {
    switch (inst->kind) {
      case ir::InstKind::Binary: {
        auto* b = static_cast<const ir::BinaryInst*>(inst);
        graph.addNode(b->dest);
        if (b->lhs.isVReg()) graph.addNode(b->lhs.vregId);
        if (b->rhs.isVReg()) graph.addNode(b->rhs.vregId);
        // Operands used in the same instruction must interfere with each other
        // because they need to be in different registers simultaneously.
        if (b->lhs.isVReg() && b->rhs.isVReg()) {
          graph.addEdge(b->lhs.vregId, b->rhs.vregId);
        }
        // The destination interferes with operands that are still live after
        // (handled by liveOut below), but also with operands used in this inst
        // if the dest is written before the operands are read (standard
        // assumption).
        if (b->lhs.isVReg()) graph.addEdge(b->dest, b->lhs.vregId);
        if (b->rhs.isVReg()) graph.addEdge(b->dest, b->rhs.vregId);
        break;
      }
      case ir::InstKind::Unary: {
        auto* u = static_cast<const ir::UnaryInst*>(inst);
        graph.addNode(u->dest);
        if (u->operand.isVReg()) {
          graph.addNode(u->operand.vregId);
          graph.addEdge(u->dest, u->operand.vregId);
        }
        break;
      }
      case ir::InstKind::Copy: {
        auto* c = static_cast<const ir::CopyInst*>(inst);
        graph.addNode(c->dest);
        if (c->src.isVReg()) graph.addNode(c->src.vregId);
        // Note: For copy instructions, we intentionally do NOT add an edge
        // between dest and src. This allows copy coalescing (same register).
        // The dest will still interfere with anything live-out from this inst.
        break;
      }
      case ir::InstKind::Load: {
        auto* l = static_cast<const ir::LoadInst*>(inst);
        graph.addNode(l->dest);
        if (l->addr.isVReg()) {
          graph.addNode(l->addr.vregId);
          graph.addEdge(l->dest, l->addr.vregId);
        }
        break;
      }
      case ir::InstKind::Store: {
        auto* s = static_cast<const ir::StoreInst*>(inst);
        if (s->src.isVReg()) graph.addNode(s->src.vregId);
        if (s->addr.isVReg()) graph.addNode(s->addr.vregId);
        // src and addr must be in different registers
        if (s->src.isVReg() && s->addr.isVReg()) {
          graph.addEdge(s->src.vregId, s->addr.vregId);
        }
        break;
      }
      case ir::InstKind::Branch: {
        auto* br = static_cast<const ir::BranchInst*>(inst);
        if (br->cond.isVReg()) graph.addNode(br->cond.vregId);
        break;
      }
      case ir::InstKind::Call: {
        auto* c = static_cast<const ir::CallInst*>(inst);
        if (c->hasDest) graph.addNode(c->dest);
        // All arguments must be in different registers
        std::vector<int> argVRegs;
        for (const auto& a : c->args) {
          if (a.isVReg()) {
            graph.addNode(a.vregId);
            argVRegs.push_back(a.vregId);
          }
        }
        // Add edges between all pairs of vreg arguments
        for (size_t i = 0; i < argVRegs.size(); ++i) {
          for (size_t j = i + 1; j < argVRegs.size(); ++j) {
            graph.addEdge(argVRegs[i], argVRegs[j]);
          }
        }
        break;
      }
      case ir::InstKind::Return: {
        auto* r = static_cast<const ir::ReturnInst*>(inst);
        if (r->hasValue && r->value.isVReg()) graph.addNode(r->value.vregId);
        break;
      }
      case ir::InstKind::Label:
      case ir::InstKind::Jump:
        break;
    }
  }

  // For each instruction, the destination register interferes with all
  // variables that are live-out (they need to coexist after the instruction).
  for (size_t i = 0; i < liveness.liveOut.size(); ++i) {
    const auto& liveSet = liveness.liveOut[i];
    const auto* inst = liveness.order[i];

    // Add all live variables as nodes
    for (int vreg : liveSet) {
      graph.addNode(vreg);
    }

    // Every pair of simultaneously live variables interferes
    std::vector<int> liveVec(liveSet.begin(), liveSet.end());
    for (size_t j = 0; j < liveVec.size(); ++j) {
      for (size_t k = j + 1; k < liveVec.size(); ++k) {
        graph.addEdge(liveVec[j], liveVec[k]);
      }
    }

    // The defined register interferes with everything live-out
    int def = -1;
    switch (inst->kind) {
      case ir::InstKind::Binary:
        def = static_cast<const ir::BinaryInst*>(inst)->dest;
        break;
      case ir::InstKind::Unary:
        def = static_cast<const ir::UnaryInst*>(inst)->dest;
        break;
      case ir::InstKind::Copy:
        def = static_cast<const ir::CopyInst*>(inst)->dest;
        break;
      case ir::InstKind::Load:
        def = static_cast<const ir::LoadInst*>(inst)->dest;
        break;
      case ir::InstKind::Call: {
        auto* c = static_cast<const ir::CallInst*>(inst);
        if (c->hasDest) def = c->dest;
        break;
      }
      default:
        break;
    }
    if (def >= 0) {
      for (int vreg : liveSet) {
        if (vreg != def) {
          graph.addEdge(def, vreg);
        }
      }
    }
  }

  return graph;
}

// Simplify phase: repeatedly remove nodes with degree < K; if none, pick a
// spill candidate by heuristic.
std::vector<int> GraphColoringAllocator::simplify(
    InterferenceGraph& graph, int K,
    const std::unordered_map<int, int>& spillCost,
    std::unordered_set<int>& spilled, bool& needsRetry) {
  std::vector<int> stack;

  while (!graph.nodes.empty()) {
    bool removed = false;
    for (int node : graph.nodes) {
      if (graph.degree(node) < K) {
        stack.push_back(node);
        graph.removeNode(node);
        removed = true;
        break;
      }
    }

    if (removed) continue;

    // No low-degree node; select a spill candidate.
    int candidate = selectSpillCandidate(graph, spillCost);
    if (candidate == -1) break;
    needsRetry = true;
    spilled.insert(candidate);
    stack.push_back(candidate);
    graph.removeNode(candidate);
  }

  return stack;
}

// Select a spill candidate (heuristic: lowest cost, break ties by degree).
int GraphColoringAllocator::selectSpillCandidate(
    const InterferenceGraph& graph,
    const std::unordered_map<int, int>& spillCost) {
  int best = -1;
  int bestScore = std::numeric_limits<int>::max();
  int bestDegree = -1;

  for (int node : graph.nodes) {
    int score = 1000;  // default high cost if missing
    auto it = spillCost.find(node);
    if (it != spillCost.end()) score = it->second;
    int deg = graph.degree(node);
    if (score < bestScore || (score == bestScore && deg > bestDegree)) {
      best = node;
      bestScore = score;
      bestDegree = deg;
    }
  }

  return best;
}

// Coloring phase: assign colors to nodes from the stack
bool GraphColoringAllocator::colorGraph(
    const std::vector<int>& stack, const InterferenceGraph& original,
    const std::unordered_map<int, bool>& liveAcrossCall,
    std::unordered_map<int, int>& colors, std::set<int>& spilled) {
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    int node = *it;

    std::set<int> usedColors;
    std::set<int> neighbors = original.neighbors(node);
    for (int neighbor : neighbors) {
      auto colorIt = colors.find(neighbor);
      if (colorIt != colors.end()) usedColors.insert(colorIt->second);
    }

    bool prefersCallee = false;
    auto lit = liveAcrossCall.find(node);
    if (lit != liveAcrossCall.end()) prefersCallee = lit->second;

    // Prefer callee-saved for values live across calls; otherwise caller-saved
    // Colors 0-4: t0-t4 (caller-saved), 5-12: s0-s7 (callee-saved)
    static const int callerFirst[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    static const int calleeFirst[] = {5, 6, 7, 8, 9, 10, 11, 12, 0, 1, 2, 3, 4};
    const int* order = prefersCallee ? calleeFirst : callerFirst;

    int color = -1;
    for (int idx = 0; idx < NUM_COLORS; ++idx) {
      int c = order[idx];
      if (usedColors.find(c) == usedColors.end()) {
        color = c;
        break;
      }
    }

    if (color == -1) {
      spilled.insert(node);
      continue;
    }

    colors[node] = color;
  }

  return true;
}

// Main register allocation algorithm
RegAllocResult GraphColoringAllocator::allocate(
    const ir::IRFunction& func, const LivenessResult& liveness) {
  RegAllocResult result;
  result.needsRetry = false;

  // Build interference graph
  InterferenceGraph graph = buildInterferenceGraph(func, liveness);

  if (graph.nodes.empty()) {
    // No virtual registers to allocate
    return result;
  }

  // Precompute spill cost and call-liveness info
  std::unordered_map<int, int> spillCost;
  std::unordered_map<int, bool> liveAcrossCall;

  const size_t n = liveness.order.size();
  for (size_t i = 0; i < n; ++i) {
    const ir::Instruction* inst = liveness.order[i];

    auto touch = [&](const ir::Operand& op) {
      if (!op.isVReg()) return;
      int id = op.vregId;
      int& cost = spillCost[id];
      cost += 1;  // usage count
      // approximate span: weight by position to prefer early defs
      cost += static_cast<int>(i / 8);  // mild growth with distance
    };

    switch (inst->kind) {
      case ir::InstKind::Binary: {
        auto* b = static_cast<const ir::BinaryInst*>(inst);
        touch(b->lhs);
        touch(b->rhs);
        touch(ir::Operand::VReg(b->dest));
        break;
      }
      case ir::InstKind::Unary: {
        auto* u = static_cast<const ir::UnaryInst*>(inst);
        touch(u->operand);
        touch(ir::Operand::VReg(u->dest));
        break;
      }
      case ir::InstKind::Copy: {
        auto* c = static_cast<const ir::CopyInst*>(inst);
        touch(c->src);
        touch(ir::Operand::VReg(c->dest));
        break;
      }
      case ir::InstKind::Load: {
        auto* l = static_cast<const ir::LoadInst*>(inst);
        touch(l->addr);
        touch(ir::Operand::VReg(l->dest));
        break;
      }
      case ir::InstKind::Store: {
        auto* s = static_cast<const ir::StoreInst*>(inst);
        touch(s->src);
        touch(s->addr);
        break;
      }
      case ir::InstKind::Branch: {
        auto* br = static_cast<const ir::BranchInst*>(inst);
        touch(br->cond);
        break;
      }
      case ir::InstKind::Call: {
        auto* c = static_cast<const ir::CallInst*>(inst);
        for (const auto& a : c->args) touch(a);
        // mark all live-out as crossing call
        for (int v : liveness.liveOut[i]) liveAcrossCall[v] = true;
        if (c->hasDest) touch(ir::Operand::VReg(c->dest));
        break;
      }
      case ir::InstKind::Return: {
        auto* r = static_cast<const ir::ReturnInst*>(inst);
        if (r->hasValue) touch(r->value);
        break;
      }
      default:
        break;
    }
  }

  // Also mark values live across back-edges (loops) as preferring callee-saved.
  // A simple heuristic: if a vreg is in liveIn of any block that is targeted by
  // a backward jump (loop header), mark it.
  {
    ir::ControlFlowGraph cfg =
        ir::ControlFlowGraph::Build(const_cast<ir::IRFunction*>(&func));
    std::unordered_map<std::string, size_t> labelToIdx;
    for (size_t i = 0; i < n; ++i) {
      if (liveness.order[i]->kind == ir::InstKind::Label) {
        auto* lbl = static_cast<const ir::LabelInst*>(liveness.order[i]);
        labelToIdx[lbl->label] = i;
      }
    }
    for (size_t i = 0; i < n; ++i) {
      const ir::Instruction* inst = liveness.order[i];
      std::string target;
      if (inst->kind == ir::InstKind::Jump) {
        target = static_cast<const ir::JumpInst*>(inst)->target;
      } else if (inst->kind == ir::InstKind::Branch) {
        auto* br = static_cast<const ir::BranchInst*>(inst);
        // check both targets
        auto checkTarget = [&](const std::string& t) {
          auto it = labelToIdx.find(t);
          if (it != labelToIdx.end() && it->second <= i) {
            // backward edge: mark liveIn at target as loop-carried
            for (int v : liveness.liveIn[it->second]) liveAcrossCall[v] = true;
          }
        };
        checkTarget(br->trueLabel);
        checkTarget(br->falseLabel);
        continue;
      }
      if (!target.empty()) {
        auto it = labelToIdx.find(target);
        if (it != labelToIdx.end() && it->second <= i) {
          for (int v : liveness.liveIn[it->second]) liveAcrossCall[v] = true;
        }
      }
    }
  }

  // Save original graph for coloring phase
  InterferenceGraph originalGraph = graph;

  std::unordered_set<int> spilledBySimplify;
  // Simplification phase
  std::vector<int> stack = simplify(graph, NUM_COLORS, spillCost,
                                    spilledBySimplify, result.needsRetry);

  // Coloring phase (may still spill if no color available)
  colorGraph(stack, originalGraph, liveAcrossCall, result.allocation,
             result.spilledVRegs);

  // Union spills found in simplify and coloring
  result.spilledVRegs.insert(spilledBySimplify.begin(),
                             spilledBySimplify.end());
  if (!result.spilledVRegs.empty()) result.needsRetry = true;

  return result;
}

// Map color to RISC-V register name
std::string RISCVRegMap::physicalRegName(int color) {
  // Allocatable: t0-t4 (caller-saved), s0-s7 (callee-saved)
  static const char* regNames[] = {"t0", "t1", "t2", "t3", "t4", "s0", "s1",
                                   "s2", "s3", "s4", "s5", "s6", "s7"};

  if (color >= 0 && color < 13) {
    return regNames[color];
  }

  return "unknown";
}

int RISCVRegMap::colorToPhysicalReg(int color) {
  // t0-t4: x5-x7,x28,x29; s0-s7: x8,x9,x18-x23
  static const int regNums[] = {
      5, 6, 7,  28, 29,             // t0-t4
      8, 9, 18, 19, 20, 21, 22, 23  // s0-s7
  };

  if (color >= 0 && color < 13) {
    return regNums[color];
  }

  return -1;
}

}  // namespace regalloc

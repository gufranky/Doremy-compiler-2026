#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace midir {

namespace {

enum class UseSiteKind {
  Operand,
  PhiIncoming,
};

struct UseSite {
  UseSiteKind kind = UseSiteKind::Operand;
  int user_block = -1;
  int pred_block = -1;
};

struct ValueRewriteState {
  int original_value_id = -1;
  Type value_type = Type::Void();
  const Loop* loop = nullptr;
  const DominatorTree* dom_tree = nullptr;
  const std::vector<int>* def_block = nullptr;
  const std::vector<char>* in_loop = nullptr;
  std::unordered_map<int, int> exit_phi_by_block;
  std::unordered_map<int, int> repair_phi_by_block;
  std::unordered_map<int, int> block_state;
  std::unordered_map<int, ValueRef> block_value;
};

bool loopContainsBlock(const Loop& loop, int block) {
  return std::binary_search(loop.blocks.begin(), loop.blocks.end(), block);
}

bool isExitBlock(const Loop& loop, int block) {
  return std::binary_search(loop.exit_blocks.begin(), loop.exit_blocks.end(), block);
}

bool valueRefsEqual(const ValueRef& lhs, const ValueRef& rhs) {
  return lhs.kind == rhs.kind && lhs.type == rhs.type &&
         lhs.value_id == rhs.value_id && lhs.int_value == rhs.int_value &&
         lhs.float_value == rhs.float_value &&
         lhs.frame_offset == rhs.frame_offset && lhs.symbol == rhs.symbol;
}

std::vector<int> buildDefBlocks(const Function& function) {
  std::vector<int> def_block(function.next_value_id, -1);
  for (int block_index = 0; block_index < static_cast<int>(function.blocks.size());
       ++block_index) {
    for (const auto& inst : function.blocks[block_index].instructions) {
      if (inst.has_result && inst.result_id >= 0 &&
          inst.result_id < static_cast<int>(def_block.size())) {
        def_block[inst.result_id] = block_index;
      }
    }
  }
  for (int param : function.params) {
    if (param >= 0 && param < static_cast<int>(def_block.size()) &&
        function.entry_block >= 0) {
      def_block[param] = function.entry_block;
    }
  }
  return def_block;
}

std::vector<std::vector<UseSite>> buildUseIndex(const Function& function) {
  std::vector<std::vector<UseSite>> uses(function.next_value_id);
  for (int block_index = 0; block_index < static_cast<int>(function.blocks.size());
       ++block_index) {
    const auto& block = function.blocks[block_index];
    for (const auto& inst : block.instructions) {
      if (inst.kind == InstKind::Phi) {
        for (const auto& incoming : inst.incomings) {
          if (!incoming.value.isSSA()) continue;
          if (incoming.value.value_id < 0 ||
              incoming.value.value_id >= static_cast<int>(uses.size())) {
            continue;
          }
          UseSite site;
          site.kind = UseSiteKind::PhiIncoming;
          site.user_block = block_index;
          site.pred_block = incoming.pred_block;
          uses[incoming.value.value_id].push_back(site);
        }
      }
      for (const auto& operand : inst.operands) {
        if (!operand.isSSA()) continue;
        if (operand.value_id < 0 || operand.value_id >= static_cast<int>(uses.size())) {
          continue;
        }
        UseSite site;
        site.kind = UseSiteKind::Operand;
        site.user_block = block_index;
        site.pred_block = -1;
        uses[operand.value_id].push_back(site);
      }
    }
  }
  return uses;
}

int insertPhiAtTop(Function& function, int block_index, Type value_type) {
  Instruction phi;
  phi.kind = InstKind::Phi;
  phi.result_type = value_type;
  phi.result_id = function.newValue(value_type);
  phi.has_result = true;

  auto& block = function.blocks[block_index];
  int insert_index = 0;
  while (insert_index < static_cast<int>(block.instructions.size()) &&
         block.instructions[insert_index].kind == InstKind::Phi) {
    ++insert_index;
  }
  block.instructions.insert(block.instructions.begin() + insert_index, std::move(phi));
  return block.instructions[insert_index].result_id;
}

Instruction* findPhiByResultId(Function& function, int block_index, int phi_id) {
  if (block_index < 0 || block_index >= static_cast<int>(function.blocks.size())) {
    return nullptr;
  }
  auto& block = function.blocks[block_index];
  for (auto& inst : block.instructions) {
    if (inst.kind != InstKind::Phi) break;
    if (inst.result_id == phi_id) return &inst;
  }
  return nullptr;
}

bool canMaterializeOnExit(const Function& function, const Loop& loop,
                          const DominatorTree& dom_tree,
                          const std::vector<int>& def_block, int value_id,
                          int exit_block) {
  if (value_id < 0 || value_id >= static_cast<int>(def_block.size())) return false;
  if (exit_block < 0 || exit_block >= static_cast<int>(function.blocks.size())) {
    return false;
  }
  const int defining_block = def_block[value_id];
  if (defining_block < 0) return false;

  for (int pred : function.blocks[exit_block].preds) {
    if (!loopContainsBlock(loop, pred)) return false;
    if (defining_block != pred && !dominates(dom_tree, defining_block, pred)) {
      return false;
    }
  }
  return true;
}

ValueRef ensureExitPhi(Function& function, ValueRewriteState& state, int exit_block) {
  std::unordered_map<int, int>::const_iterator it =
      state.exit_phi_by_block.find(exit_block);
  if (it != state.exit_phi_by_block.end()) {
    return ValueRef::SSA(it->second, state.value_type);
  }

  if (!canMaterializeOnExit(function, *state.loop, *state.dom_tree, *state.def_block,
                            state.original_value_id, exit_block)) {
    return ValueRef::Invalid();
  }

  const int phi_id = insertPhiAtTop(function, exit_block, state.value_type);
  Instruction* phi = findPhiByResultId(function, exit_block, phi_id);
  if (phi == nullptr) return ValueRef::Invalid();

  phi->incomings.clear();
  const ValueRef original = ValueRef::SSA(state.original_value_id, state.value_type);
  for (int pred : function.blocks[exit_block].preds) {
    phi->incomings.push_back(PhiIncoming{pred, original});
  }

  state.exit_phi_by_block[exit_block] = phi_id;
  return ValueRef::SSA(phi_id, state.value_type);
}

ValueRef ensureRepairPhiPlaceholder(Function& function, ValueRewriteState& state,
                                    int block_index) {
  std::unordered_map<int, int>::const_iterator it =
      state.repair_phi_by_block.find(block_index);
  if (it != state.repair_phi_by_block.end()) {
    return ValueRef::SSA(it->second, state.value_type);
  }

  const int phi_id = insertPhiAtTop(function, block_index, state.value_type);
  state.repair_phi_by_block[block_index] = phi_id;
  return ValueRef::SSA(phi_id, state.value_type);
}

bool allValuesEqual(const std::vector<ValueRef>& values) {
  if (values.empty()) return true;
  for (size_t i = 1; i < values.size(); ++i) {
    if (!valueRefsEqual(values[i], values.front())) return false;
  }
  return true;
}

ValueRef getValueAtBlock(Function& function, ValueRewriteState& state,
                         int block_index) {
  if (block_index < 0 || block_index >= static_cast<int>(function.blocks.size())) {
    return ValueRef::Invalid();
  }
  if ((*state.in_loop)[block_index]) {
    return ValueRef::Invalid();
  }

  std::unordered_map<int, int>::const_iterator state_it =
      state.block_state.find(block_index);
  if (state_it != state.block_state.end()) {
    if (state_it->second == 2) {
      return state.block_value[block_index];
    }
    if (state_it->second == 3) {
      return ValueRef::Invalid();
    }
    if (state_it->second == 1) {
      return ensureRepairPhiPlaceholder(function, state, block_index);
    }
  }

  if (isExitBlock(*state.loop, block_index)) {
    ValueRef exit_value = ensureExitPhi(function, state, block_index);
    if (!exit_value.isSSA()) {
      state.block_state[block_index] = 3;
      return ValueRef::Invalid();
    }
    state.block_state[block_index] = 2;
    state.block_value[block_index] = exit_value;
    return exit_value;
  }

  state.block_state[block_index] = 1;

  std::vector<int> preds;
  std::vector<ValueRef> incoming_values;
  preds.reserve(function.blocks[block_index].preds.size());
  incoming_values.reserve(function.blocks[block_index].preds.size());

  for (int pred : function.blocks[block_index].preds) {
    if ((*state.in_loop)[pred]) {
      state.block_state[block_index] = 3;
      return ValueRef::Invalid();
    }
    ValueRef pred_value = getValueAtBlock(function, state, pred);
    if (!pred_value.isSSA()) {
      state.block_state[block_index] = 3;
      return ValueRef::Invalid();
    }
    preds.push_back(pred);
    incoming_values.push_back(pred_value);
  }

  if (incoming_values.empty()) {
    state.block_state[block_index] = 3;
    return ValueRef::Invalid();
  }

  std::unordered_map<int, int>::const_iterator repair_it =
      state.repair_phi_by_block.find(block_index);
  if (repair_it != state.repair_phi_by_block.end()) {
    Instruction* phi = findPhiByResultId(function, block_index, repair_it->second);
    if (phi == nullptr) {
      state.block_state[block_index] = 3;
      return ValueRef::Invalid();
    }
    phi->incomings.clear();
    for (size_t i = 0; i < preds.size(); ++i) {
      phi->incomings.push_back(PhiIncoming{preds[i], incoming_values[i]});
    }
    ValueRef result = ValueRef::SSA(repair_it->second, state.value_type);
    state.block_state[block_index] = 2;
    state.block_value[block_index] = result;
    return result;
  }

  if (allValuesEqual(incoming_values)) {
    state.block_state[block_index] = 2;
    state.block_value[block_index] = incoming_values.front();
    return incoming_values.front();
  }

  const int phi_id = insertPhiAtTop(function, block_index, state.value_type);
  Instruction* phi = findPhiByResultId(function, block_index, phi_id);
  if (phi == nullptr) {
    state.block_state[block_index] = 3;
    return ValueRef::Invalid();
  }
  phi->incomings.clear();
  for (size_t i = 0; i < preds.size(); ++i) {
    phi->incomings.push_back(PhiIncoming{preds[i], incoming_values[i]});
  }
  state.repair_phi_by_block[block_index] = phi_id;

  ValueRef result = ValueRef::SSA(phi_id, state.value_type);
  state.block_state[block_index] = 2;
  state.block_value[block_index] = result;
  return result;
}

std::vector<int> collectLiveOutValues(const Function& function, const Loop& loop,
                                      const std::vector<char>& loop_defined,
                                      const std::vector<std::vector<UseSite>>& uses_by_value) {
  std::vector<int> values;
  values.reserve(function.next_value_id);
  for (int value_id = 0; value_id < static_cast<int>(loop_defined.size()); ++value_id) {
    if (!loop_defined[value_id]) continue;
    bool live_out = false;
    for (const auto& use : uses_by_value[value_id]) {
      const int use_block =
          use.kind == UseSiteKind::PhiIncoming ? use.pred_block : use.user_block;
      if (use_block < 0 || use_block >= static_cast<int>(function.blocks.size())) {
        continue;
      }
      if (!loopContainsBlock(loop, use_block)) {
        live_out = true;
        break;
      }
    }
    if (live_out) {
      values.push_back(value_id);
    }
  }
  return values;
}

bool rewriteLoopUses(Function& function, const Loop& loop,
                     const std::vector<char>& loop_defined,
                     std::unordered_map<int, ValueRewriteState>& rewrite_states) {
  bool changed = false;

  for (int block_index = 0; block_index < static_cast<int>(function.blocks.size());
       ++block_index) {
    bool revisit_block = true;
    while (revisit_block) {
      revisit_block = false;
      for (int inst_index = 0;
           inst_index < static_cast<int>(function.blocks[block_index].instructions.size());
           ++inst_index) {
        const size_t block_size_before =
            function.blocks[block_index].instructions.size();
        if (function.blocks[block_index].instructions[inst_index].kind ==
            InstKind::Phi) {
          const int incoming_count = static_cast<int>(
              function.blocks[block_index].instructions[inst_index].incomings.size());
          for (int incoming_index = 0; incoming_index < incoming_count;
               ++incoming_index) {
            const PhiIncoming incoming =
                function.blocks[block_index].instructions[inst_index]
                    .incomings[incoming_index];
            if (!incoming.value.isSSA()) continue;
            if (incoming.value.value_id < 0 ||
                incoming.value.value_id >= static_cast<int>(loop_defined.size()) ||
                !loop_defined[incoming.value.value_id]) {
              continue;
            }
            if (incoming.pred_block < 0 ||
                incoming.pred_block >= static_cast<int>(function.blocks.size()) ||
                loopContainsBlock(loop, incoming.pred_block)) {
              continue;
            }
            auto state_it = rewrite_states.find(incoming.value.value_id);
            if (state_it == rewrite_states.end()) continue;

            ValueRef replacement =
                getValueAtBlock(function, state_it->second, incoming.pred_block);
            if (function.blocks[block_index].instructions.size() != block_size_before) {
              revisit_block = true;
              break;
            }
            if (!replacement.isSSA() ||
                valueRefsEqual(replacement, incoming.value)) {
              continue;
            }
            function.blocks[block_index]
                .instructions[inst_index]
                .incomings[incoming_index]
                .value = replacement;
            changed = true;
          }
          if (revisit_block) break;
          continue;
        }

        if (loopContainsBlock(loop, block_index)) continue;
        const int operand_count = static_cast<int>(
            function.blocks[block_index].instructions[inst_index].operands.size());
        for (int operand_index = 0; operand_index < operand_count;
             ++operand_index) {
          const ValueRef operand =
              function.blocks[block_index].instructions[inst_index]
                  .operands[operand_index];
          if (!operand.isSSA()) continue;
          if (operand.value_id < 0 ||
              operand.value_id >= static_cast<int>(loop_defined.size()) ||
              !loop_defined[operand.value_id]) {
            continue;
          }
          auto state_it = rewrite_states.find(operand.value_id);
          if (state_it == rewrite_states.end()) continue;

          ValueRef replacement =
              getValueAtBlock(function, state_it->second, block_index);
          if (function.blocks[block_index].instructions.size() != block_size_before) {
            revisit_block = true;
            break;
          }
          if (!replacement.isSSA() || valueRefsEqual(replacement, operand)) continue;
          function.blocks[block_index]
              .instructions[inst_index]
              .operands[operand_index] = replacement;
          changed = true;
        }
        if (revisit_block) break;
      }
    }
  }

  return changed;
}

bool formLCSSAForLoop(Function& function, const Loop& loop,
                      const DominatorTree& dom_tree) {
  if (loop.exit_blocks.empty()) return false;

  const std::vector<int> def_block = buildDefBlocks(function);
  const std::vector<std::vector<UseSite>> uses_by_value = buildUseIndex(function);

  std::vector<char> in_loop(function.blocks.size(), 0);
  std::vector<char> loop_defined(function.next_value_id, 0);
  for (int block_index : loop.blocks) {
    if (block_index < 0 || block_index >= static_cast<int>(function.blocks.size())) {
      continue;
    }
    in_loop[block_index] = 1;
    for (const auto& inst : function.blocks[block_index].instructions) {
      if (!inst.has_result || inst.result_id < 0 || inst.result_id >= function.next_value_id) {
        continue;
      }
      loop_defined[inst.result_id] = 1;
    }
  }

  const std::vector<int> live_out_values =
      collectLiveOutValues(function, loop, loop_defined, uses_by_value);
  if (live_out_values.empty()) return false;

  std::unordered_map<int, ValueRewriteState> rewrite_states;
  for (int value_id : live_out_values) {
    ValueRewriteState state;
    state.original_value_id = value_id;
    state.value_type = function.value_types[value_id];
    state.loop = &loop;
    state.dom_tree = &dom_tree;
    state.def_block = &def_block;
    state.in_loop = &in_loop;
    rewrite_states.emplace(value_id, std::move(state));
  }

  return rewriteLoopUses(function, loop, loop_defined, rewrite_states);
}

}  // namespace

std::string LCSSAPass::name() const { return "lcssa"; }

PassResult LCSSAPass::run(Function& function,
                          AnalysisManager& analysisManager) {
  rebuildEdges(function);
  const DominatorTree& dom_tree = analysisManager.getDominatorTree(function);
  const LoopInfo& loop_info = analysisManager.getLoopInfo(function);

  std::vector<int> order(loop_info.loops.size());
  for (int i = 0; i < static_cast<int>(order.size()); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
    return loop_info.loops[lhs].depth > loop_info.loops[rhs].depth;
  });

  bool changed = false;
  for (int loop_index : order) {
    changed = formLCSSAForLoop(function, loop_info.loops[loop_index], dom_tree) ||
              changed;
  }

  if (changed) {
    normalizePhiIncomings(function);
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

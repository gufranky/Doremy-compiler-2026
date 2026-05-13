#include "optimizer_pipeline.h"

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "optimizer_utils.h"

namespace midir {

namespace {

enum class ResolveCacheState : char {
  Unknown,
  Resolving,
  Invalid,
  Resolved,
};

struct ResolveContext {
  const Module& module;
  const Function& function;
  const std::vector<const Instruction*>& def_inst_by_value_id;
  std::unordered_map<std::string, std::optional<int>> const_global_cache;
  std::vector<ValueRef> resolved_values;
  std::vector<ResolveCacheState> resolved_states;

  ResolveContext(const Module& module_in, const Function& function_in,
                 const std::vector<const Instruction*>& def_index)
      : module(module_in),
        function(function_in),
        def_inst_by_value_id(def_index),
        resolved_values(function_in.next_value_id, ValueRef::Invalid()),
        resolved_states(function_in.next_value_id, ResolveCacheState::Unknown) {}
};

bool sameValue(const ValueRef& lhs, const ValueRef& rhs) {
  return lhs.kind == rhs.kind && lhs.type == rhs.type && lhs.value_id == rhs.value_id &&
         lhs.int_value == rhs.int_value && lhs.float_value == rhs.float_value &&
         lhs.frame_offset == rhs.frame_offset && lhs.symbol == rhs.symbol;
}

std::vector<const Instruction*> buildDefInstructionIndex(const Function& function) {
  std::vector<const Instruction*> defs(function.next_value_id, nullptr);
  for (const auto& block : function.blocks) {
    for (const auto& inst : block.instructions) {
      if (!inst.has_result || inst.result_id < 0 || inst.result_id >= function.next_value_id) {
        continue;
      }
      defs[inst.result_id] = &inst;
    }
  }
  return defs;
}

std::optional<int> lookupConstGlobalInt(ResolveContext& context, const std::string& symbol) {
  auto it = context.const_global_cache.find(symbol);
  if (it != context.const_global_cache.end()) return it->second;

  std::optional<int> constant;
  for (const auto& global : context.module.globals) {
    if (global.name != symbol || !global.is_const || global.value_type != Type::I32()) {
      continue;
    }
    if (global.initial_value.isInt()) {
      constant = global.initial_value.intValue;
    }
    break;
  }
  context.const_global_cache.emplace(symbol, constant);
  return constant;
}

ValueRef tryResolveConstValue(ResolveContext& context, ValueRef value) {
  if (!value.isSSA()) return value;

  const int currentValueId = value.value_id;
  if (currentValueId < 0 || currentValueId >= context.function.next_value_id) {
    return ValueRef::Invalid();
  }

  ResolveCacheState& state = context.resolved_states[currentValueId];
  if (state == ResolveCacheState::Resolved) {
    return context.resolved_values[currentValueId];
  }
  if (state == ResolveCacheState::Invalid || state == ResolveCacheState::Resolving) {
    return ValueRef::Invalid();
  }

  const Instruction* def = context.def_inst_by_value_id[currentValueId];
  if (def == nullptr) {
    state = ResolveCacheState::Invalid;
    return ValueRef::Invalid();
  }

  state = ResolveCacheState::Resolving;
  ValueRef resolved = ValueRef::Invalid();
  if (def->kind == InstKind::Copy && def->operands.size() == 1) {
    resolved = tryResolveConstValue(context, def->operands[0]);
  } else if (def->kind == InstKind::Load && def->operands.size() == 1 &&
             def->result_type == Type::I32() &&
             def->operands[0].kind == ValueRef::Kind::GlobalSymbol) {
    auto constant = lookupConstGlobalInt(context, def->operands[0].symbol);
    if (constant.has_value()) {
      resolved = ValueRef::ImmediateInt(*constant);
    }
  }

  if (!resolved.isValid()) {
    state = ResolveCacheState::Invalid;
    context.resolved_values[currentValueId] = ValueRef::Invalid();
    return ValueRef::Invalid();
  }

  context.resolved_values[currentValueId] = resolved;
  state = ResolveCacheState::Resolved;
  return resolved;
}

void invalidateResolvedValue(ResolveContext& context, int valueId) {
  if (valueId < 0 || valueId >= context.function.next_value_id) return;
  context.resolved_states[valueId] = ResolveCacheState::Unknown;
  context.resolved_values[valueId] = ValueRef::Invalid();
}

bool isConstDivRemCandidate(const Instruction& inst) {
  return inst.kind == InstKind::Binary && inst.operands.size() == 2 &&
         inst.operand_type == Type::I32() && inst.result_type == Type::I32() &&
         (inst.binary_op == ir::BinaryOp::Div || inst.binary_op == ir::BinaryOp::Mod);
}

bool hasSSAOperands(const Instruction& inst) {
  for (const auto& operand : inst.operands) {
    if (operand.isSSA()) return true;
  }
  return false;
}

ValueRef foldBinaryInt(ir::BinaryOp op, int lhs, int rhs) {
  switch (op) {
    case ir::BinaryOp::Add:
      return ValueRef::ImmediateInt(lhs + rhs);
    case ir::BinaryOp::Sub:
      return ValueRef::ImmediateInt(lhs - rhs);
    case ir::BinaryOp::Mul:
      return ValueRef::ImmediateInt(lhs * rhs);
    case ir::BinaryOp::Div:
      if (rhs != 0) return ValueRef::ImmediateInt(lhs / rhs);
      break;
    case ir::BinaryOp::Mod:
      if (rhs != 0) return ValueRef::ImmediateInt(lhs % rhs);
      break;
    default:
      break;
  }
  return ValueRef::Invalid();
}

bool normalizeConstOperands(ResolveContext& context, Instruction& inst) {
  if (!isConstDivRemCandidate(inst) || !hasSSAOperands(inst)) {
    return false;
  }

  bool changed = false;
  for (auto& operand : inst.operands) {
    ValueRef resolved = tryResolveConstValue(context, operand);
    if (!resolved.isValid() || resolved.type != operand.type || sameValue(resolved, operand)) {
      continue;
    }
    operand = resolved;
    changed = true;
  }
  return changed;
}

bool simplifyConstDivRem(Instruction& inst) {
  if (inst.kind != InstKind::Binary || inst.operands.size() != 2 ||
      inst.operand_type != Type::I32() || inst.result_type != Type::I32()) {
    return false;
  }
  if (inst.binary_op != ir::BinaryOp::Div && inst.binary_op != ir::BinaryOp::Mod) {
    return false;
  }

  const ValueRef& lhs = inst.operands[0];
  const ValueRef& rhs = inst.operands[1];
  if (rhs.kind != ValueRef::Kind::ImmediateInt) return false;

  if (lhs.kind == ValueRef::Kind::ImmediateInt) {
    ValueRef folded = foldBinaryInt(inst.binary_op, lhs.int_value, rhs.int_value);
    if (!folded.isValid()) return false;
    inst = makeCopyInstruction(inst, folded);
    return true;
  }

  if (inst.binary_op == ir::BinaryOp::Div) {
    if (rhs.int_value == 1) {
      inst = makeCopyInstruction(inst, lhs);
      return true;
    }
    if (rhs.int_value == -1 && lhs.kind == ValueRef::Kind::ImmediateInt) {
      inst = makeCopyInstruction(inst, ValueRef::ImmediateInt(-lhs.int_value));
      return true;
    }
  } else {
    if (rhs.int_value == 1 || rhs.int_value == -1) {
      inst = makeCopyInstruction(inst, ValueRef::ImmediateInt(0));
      return true;
    }
  }
  return false;
}

}  // namespace

std::string ConstDivRemPass::name() const { return "const-div-rem"; }

PassResult ConstDivRemPass::run(Function& function, AnalysisManager& analysisManager) {
  if (module_ == nullptr || function.blocks.empty() || function.next_value_id <= 0) {
    return PassResult{false};
  }

  std::vector<const Instruction*> defInstByValueId = buildDefInstructionIndex(function);
  ResolveContext resolveContext(*module_, function, defInstByValueId);

  bool changed = false;
  for (auto& block : function.blocks) {
    for (auto& inst : block.instructions) {
      bool instChanged = normalizeConstOperands(resolveContext, inst);
      instChanged = simplifyConstDivRem(inst) || instChanged;
      if (instChanged && inst.has_result) {
        invalidateResolvedValue(resolveContext, inst.result_id);
      }
      changed = instChanged || changed;
    }
  }

  if (changed) {
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

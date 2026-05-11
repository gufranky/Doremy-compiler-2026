#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace midir {

namespace {

std::string valueKey(const ValueRef& value) {
  switch (value.kind) {
    case ValueRef::Kind::SSA:
      return "s" + std::to_string(value.value_id) + ":" + std::to_string(static_cast<int>(value.type.kind));
    case ValueRef::Kind::ImmediateInt:
      return "i" + std::to_string(value.int_value);
    case ValueRef::Kind::ImmediateFloat:
      return "f" + std::to_string(value.float_value);
    case ValueRef::Kind::GlobalSymbol:
      return "g" + value.symbol;
    case ValueRef::Kind::FrameAddress:
      return "fa" + std::to_string(value.frame_offset);
    case ValueRef::Kind::StackPointer:
      return "sp";
    case ValueRef::Kind::Undef:
      return "u" + std::to_string(static_cast<int>(value.type.kind));
    case ValueRef::Kind::Invalid:
      return "invalid";
  }
  return "invalid";
}

std::string instructionKey(const Instruction& inst) {
  if (inst.kind == InstKind::Binary && inst.operands.size() == 2) {
    ValueRef lhs = inst.operands[0];
    ValueRef rhs = inst.operands[1];
    if (isCommutativeBinaryOp(inst.binary_op) && valueKey(rhs) < valueKey(lhs)) {
      std::swap(lhs, rhs);
    }
    return "b:" + std::to_string(static_cast<int>(inst.binary_op)) + ":" +
           std::to_string(static_cast<int>(inst.operand_type.kind)) + ":" +
           valueKey(lhs) + ":" + valueKey(rhs);
  }
  if (inst.kind == InstKind::Unary && inst.operands.size() == 1) {
    return "u:" + std::to_string(static_cast<int>(inst.unary_op)) + ":" +
           std::to_string(static_cast<int>(inst.operand_type.kind)) + ":" +
           valueKey(inst.operands[0]);
  }
  if (inst.kind == InstKind::Copy && inst.operands.size() == 1) {
    return "c:" + std::to_string(static_cast<int>(inst.result_type.kind)) + ":" +
           valueKey(inst.operands[0]);
  }
  return {};
}

}  // namespace

std::string EarlyCSEPass::name() const { return "early-cse"; }

PassResult EarlyCSEPass::run(Function& function,
                             AnalysisManager& analysisManager) {
  bool changed = false;
  for (auto& block : function.blocks) {
    std::unordered_map<std::string, ValueRef> available;
    for (auto& inst : block.instructions) {
      if (!inst.has_result || !isPureComputingInstruction(inst) ||
          inst.kind == InstKind::Phi || inst.kind == InstKind::Load) {
        continue;
      }
      const std::string key = instructionKey(inst);
      if (key.empty()) continue;
      auto it = available.find(key);
      if (it != available.end()) {
        inst = makeCopyInstruction(inst, it->second);
        changed = true;
        continue;
      }
      available.emplace(key, ValueRef::SSA(inst.result_id, inst.result_type));
    }
  }
  if (changed) {
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

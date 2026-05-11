#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>

namespace midir {

namespace {

bool isZeroValue(const ValueRef& value) {
  return (value.kind == ValueRef::Kind::ImmediateInt && value.int_value == 0) ||
         (value.kind == ValueRef::Kind::ImmediateFloat && value.float_value == 0.0f);
}

bool isOneValue(const ValueRef& value) {
  return (value.kind == ValueRef::Kind::ImmediateInt && value.int_value == 1) ||
         (value.kind == ValueRef::Kind::ImmediateFloat && value.float_value == 1.0f);
}

ValueRef foldBinary(const Instruction& inst) {
  const auto& lhs = inst.operands[0];
  const auto& rhs = inst.operands[1];
  if (lhs.kind == ValueRef::Kind::ImmediateInt &&
      rhs.kind == ValueRef::Kind::ImmediateInt) {
    switch (inst.binary_op) {
      case ir::BinaryOp::Add:
        return ValueRef::ImmediateInt(lhs.int_value + rhs.int_value);
      case ir::BinaryOp::Sub:
        return ValueRef::ImmediateInt(lhs.int_value - rhs.int_value);
      case ir::BinaryOp::Mul:
        return ValueRef::ImmediateInt(lhs.int_value * rhs.int_value);
      case ir::BinaryOp::Div:
        if (rhs.int_value != 0) return ValueRef::ImmediateInt(lhs.int_value / rhs.int_value);
        break;
      case ir::BinaryOp::Mod:
        if (rhs.int_value != 0) return ValueRef::ImmediateInt(lhs.int_value % rhs.int_value);
        break;
      case ir::BinaryOp::And:
        return ValueRef::ImmediateInt(lhs.int_value && rhs.int_value);
      case ir::BinaryOp::Or:
        return ValueRef::ImmediateInt(lhs.int_value || rhs.int_value);
      case ir::BinaryOp::Lt:
        return ValueRef::ImmediateInt(lhs.int_value < rhs.int_value);
      case ir::BinaryOp::Gt:
        return ValueRef::ImmediateInt(lhs.int_value > rhs.int_value);
      case ir::BinaryOp::Le:
        return ValueRef::ImmediateInt(lhs.int_value <= rhs.int_value);
      case ir::BinaryOp::Ge:
        return ValueRef::ImmediateInt(lhs.int_value >= rhs.int_value);
      case ir::BinaryOp::Eq:
        return ValueRef::ImmediateInt(lhs.int_value == rhs.int_value);
      case ir::BinaryOp::Ne:
        return ValueRef::ImmediateInt(lhs.int_value != rhs.int_value);
    }
  }
  if (lhs.kind == ValueRef::Kind::ImmediateFloat &&
      rhs.kind == ValueRef::Kind::ImmediateFloat) {
    switch (inst.binary_op) {
      case ir::BinaryOp::Add:
        return ValueRef::ImmediateFloat(lhs.float_value + rhs.float_value);
      case ir::BinaryOp::Sub:
        return ValueRef::ImmediateFloat(lhs.float_value - rhs.float_value);
      case ir::BinaryOp::Mul:
        return ValueRef::ImmediateFloat(lhs.float_value * rhs.float_value);
      case ir::BinaryOp::Div:
        if (rhs.float_value != 0.0f) {
          return ValueRef::ImmediateFloat(lhs.float_value / rhs.float_value);
        }
        break;
      case ir::BinaryOp::Lt:
        return ValueRef::ImmediateInt(lhs.float_value < rhs.float_value);
      case ir::BinaryOp::Gt:
        return ValueRef::ImmediateInt(lhs.float_value > rhs.float_value);
      case ir::BinaryOp::Le:
        return ValueRef::ImmediateInt(lhs.float_value <= rhs.float_value);
      case ir::BinaryOp::Ge:
        return ValueRef::ImmediateInt(lhs.float_value >= rhs.float_value);
      case ir::BinaryOp::Eq:
        return ValueRef::ImmediateInt(lhs.float_value == rhs.float_value);
      case ir::BinaryOp::Ne:
        return ValueRef::ImmediateInt(lhs.float_value != rhs.float_value);
      case ir::BinaryOp::Mod:
      case ir::BinaryOp::And:
      case ir::BinaryOp::Or:
        break;
    }
  }
  return ValueRef::Invalid();
}

ValueRef foldUnary(const Instruction& inst) {
  const auto& operand = inst.operands[0];
  if (operand.kind == ValueRef::Kind::ImmediateInt) {
    switch (inst.unary_op) {
      case ir::UnaryOp::Plus:
        return operand;
      case ir::UnaryOp::Neg:
        return ValueRef::ImmediateInt(-operand.int_value);
      case ir::UnaryOp::Not:
        return ValueRef::ImmediateInt(!operand.int_value);
    }
  }
  if (operand.kind == ValueRef::Kind::ImmediateFloat) {
    switch (inst.unary_op) {
      case ir::UnaryOp::Plus:
        return operand;
      case ir::UnaryOp::Neg:
        return ValueRef::ImmediateFloat(-operand.float_value);
      case ir::UnaryOp::Not:
        break;
    }
  }
  return ValueRef::Invalid();
}

bool tryCanonicalizeCommutative(Instruction& inst) {
  if (inst.kind != InstKind::Binary || !isCommutativeBinaryOp(inst.binary_op) ||
      inst.operands.size() != 2) {
    return false;
  }
  if (inst.operands[0].isImmediate() && !inst.operands[1].isImmediate()) {
    std::swap(inst.operands[0], inst.operands[1]);
    return true;
  }
  return false;
}

bool trySimplifyIdentity(Instruction& inst) {
  if (inst.kind == InstKind::Copy && inst.operands.size() == 1 &&
      inst.operands[0].isSSA() && inst.operands[0].value_id == inst.result_id) {
    inst.operands[0] = ValueRef::Undef(inst.result_type);
    return true;
  }
  if (inst.kind != InstKind::Binary || inst.operands.size() != 2) return false;

  const ValueRef& lhs = inst.operands[0];
  const ValueRef& rhs = inst.operands[1];
  switch (inst.binary_op) {
    case ir::BinaryOp::Add:
      if (isZeroValue(rhs)) {
        inst = makeCopyInstruction(inst, lhs);
        return true;
      }
      if (isZeroValue(lhs)) {
        inst = makeCopyInstruction(inst, rhs);
        return true;
      }
      break;
    case ir::BinaryOp::Sub:
      if (isZeroValue(rhs)) {
        inst = makeCopyInstruction(inst, lhs);
        return true;
      }
      break;
    case ir::BinaryOp::Mul:
      if (isOneValue(rhs)) {
        inst = makeCopyInstruction(inst, lhs);
        return true;
      }
      if (isOneValue(lhs)) {
        inst = makeCopyInstruction(inst, rhs);
        return true;
      }
      if (isZeroValue(rhs)) {
        inst = makeCopyInstruction(inst, rhs);
        return true;
      }
      if (isZeroValue(lhs)) {
        inst = makeCopyInstruction(inst, lhs);
        return true;
      }
      break;
    case ir::BinaryOp::Div:
      if (isOneValue(rhs)) {
        inst = makeCopyInstruction(inst, lhs);
        return true;
      }
      break;
    default:
      break;
  }
  return false;
}

}  // namespace

std::string InstCombinePass::name() const { return "instcombine"; }

PassResult InstCombinePass::run(Function& function,
                                AnalysisManager& analysisManager) {
  bool changed = false;
  for (auto& block : function.blocks) {
    for (auto& inst : block.instructions) {
      changed = tryCanonicalizeCommutative(inst) || changed;
      changed = trySimplifyIdentity(inst) || changed;
      if (inst.kind == InstKind::Binary && inst.operands.size() == 2) {
        ValueRef folded = foldBinary(inst);
        if (folded.isValid()) {
          inst = makeCopyInstruction(inst, folded);
          changed = true;
        }
      } else if (inst.kind == InstKind::Unary && inst.operands.size() == 1) {
        ValueRef folded = foldUnary(inst);
        if (folded.isValid()) {
          inst = makeCopyInstruction(inst, folded);
          changed = true;
        }
      }
    }
  }
  if (changed) {
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

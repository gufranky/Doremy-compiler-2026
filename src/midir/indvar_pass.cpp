#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <string>
#include <unordered_map>

#include <algorithm>
#include <numeric>
#include <optional>
#include <vector>

namespace midir {

namespace {

struct AddressRecurrenceCandidate {
  int address_result = -1;
  Type result_type = Type::Ptr();
  Type operand_type = Type::I32();
  ValueRef invariant_base = ValueRef::Invalid();
  ValueRef invariant_offset = ValueRef::Invalid();
  int invariant_offset_scale = 0;
  int stride = 0;
  int bias = 0;
};

struct AffineIvExpr {
  int scale = 0;
  ValueRef invariant = ValueRef::Invalid();
  int invariant_scale = 0;
  int bias = 0;
};

struct AffineMatchCache {
  std::vector<std::optional<AffineIvExpr>> values;
  std::vector<char> visiting;
  std::vector<char> known_miss;
};

struct MemoryBaseKey {
  enum class Kind { Invalid, Global, Frame, Param, Stack, Unknown };

  Kind kind = Kind::Invalid;
  std::string symbol;
  int frame_offset = 0;
  int param_index = -1;

  bool isIdentifiable() const {
    return kind != Kind::Invalid && kind != Kind::Unknown;
  }
};

std::optional<int> getConstantInt(const ValueRef& value) {
  if (value.kind == ValueRef::Kind::ImmediateInt) return value.int_value;
  return std::nullopt;
}

bool sameValueRef(const ValueRef& lhs, const ValueRef& rhs) {
  return lhs.kind == rhs.kind && lhs.type == rhs.type && lhs.value_id == rhs.value_id &&
         lhs.int_value == rhs.int_value && lhs.float_value == rhs.float_value &&
         lhs.frame_offset == rhs.frame_offset && lhs.symbol == rhs.symbol;
}

std::optional<AffineIvExpr> combineAffineAdd(const AffineIvExpr& lhs,
                                             const AffineIvExpr& rhs) {
  if (lhs.invariant_scale != 0 && rhs.invariant_scale != 0 &&
      !sameValueRef(lhs.invariant, rhs.invariant)) {
    return std::nullopt;
  }
  AffineIvExpr result;
  result.scale = lhs.scale + rhs.scale;
  result.bias = lhs.bias + rhs.bias;
  if (lhs.invariant_scale != 0) {
    result.invariant = lhs.invariant;
    result.invariant_scale = lhs.invariant_scale;
  }
  if (rhs.invariant_scale != 0) {
    result.invariant = rhs.invariant;
    result.invariant_scale += rhs.invariant_scale;
  }
  return result;
}

std::optional<AffineIvExpr> combineAffineSub(const AffineIvExpr& lhs,
                                             const AffineIvExpr& rhs) {
  if (lhs.invariant_scale != 0 && rhs.invariant_scale != 0 &&
      !sameValueRef(lhs.invariant, rhs.invariant)) {
    return std::nullopt;
  }
  AffineIvExpr result;
  result.scale = lhs.scale - rhs.scale;
  result.bias = lhs.bias - rhs.bias;
  if (lhs.invariant_scale != 0) {
    result.invariant = lhs.invariant;
    result.invariant_scale = lhs.invariant_scale;
  }
  if (rhs.invariant_scale != 0) {
    result.invariant = rhs.invariant;
    result.invariant_scale -= rhs.invariant_scale;
  }
  return result;
}

std::optional<AffineIvExpr> multiplyAffineByConstant(const AffineIvExpr& expr,
                                                     int constant) {
  AffineIvExpr result;
  result.scale = expr.scale * constant;
  result.bias = expr.bias * constant;
  result.invariant = expr.invariant;
  result.invariant_scale = expr.invariant_scale * constant;
  return result;
}

bool loopContainsBlock(const Loop& loop, int blockIndex) {
  return std::find(loop.blocks.begin(), loop.blocks.end(), blockIndex) !=
         loop.blocks.end();
}

bool isParameterValue(const Function& function, int valueId) {
  return std::find(function.params.begin(), function.params.end(), valueId) !=
         function.params.end();
}

int findPointerParamIndex(const Function& function, int valueId) {
  for (size_t i = 0; i < function.params.size(); ++i) {
    if (function.params[i] != valueId) continue;
    if (i >= function.param_types.size()) return -1;
    if (function.param_types[i] == Type::Ptr()) return static_cast<int>(i);
    if (i < function.param_is_array.size() && function.param_is_array[i]) {
      return static_cast<int>(i);
    }
    return -1;
  }
  return -1;
}

std::vector<int> buildDefBlocks(const Function& function) {
  std::vector<int> defBlock(function.next_value_id, -1);
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size());
       ++blockIndex) {
    for (const auto& inst : function.blocks[blockIndex].instructions) {
      if (inst.has_result && inst.result_id >= 0 &&
          inst.result_id < static_cast<int>(defBlock.size())) {
        defBlock[inst.result_id] = blockIndex;
      }
    }
  }
  return defBlock;
}

const Instruction* findDefInstruction(const Function& function, int valueId,
                                      int& blockIndexOut) {
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size());
       ++blockIndex) {
    for (const auto& inst : function.blocks[blockIndex].instructions) {
      if (inst.has_result && inst.result_id == valueId) {
        blockIndexOut = blockIndex;
        return &inst;
      }
    }
  }
  blockIndexOut = -1;
  return nullptr;
}

bool isPreheaderAvailableValue(const Function& function, const Loop& loop,
                              const std::vector<int>& defBlock,
                              const DominatorTree& domTree, int valueId) {
  if (valueId < 0 || valueId >= static_cast<int>(defBlock.size())) return false;
  const int blockIndex = defBlock[valueId];
  if (blockIndex < 0) return isParameterValue(function, valueId);
  if (loopContainsBlock(loop, blockIndex)) return false;
  if (loop.header >= 0 && !dominates(domTree, blockIndex, loop.header)) return false;
  return true;
}

bool isPreheaderAvailableOperand(const Function& function, const Loop& loop,
                                const std::vector<int>& defBlock,
                                const DominatorTree& domTree,
                                const ValueRef& value) {
  if (!value.isSSA()) return true;
  return isPreheaderAvailableValue(function, loop, defBlock, domTree,
                                   value.value_id);
}

bool isLoopInvariantValue(const Function& function, const Loop& loop,
                          const std::vector<int>& defBlock, int valueId,
                          std::vector<char>& visiting,
                          const DominatorTree* domTree = nullptr) {
  if (valueId < 0 || valueId >= static_cast<int>(defBlock.size())) return false;
  const int blockIndex = defBlock[valueId];
  if (blockIndex < 0) return isParameterValue(function, valueId);
  if (!loopContainsBlock(loop, blockIndex)) {
    if (domTree != nullptr && loop.header >= 0 &&
        !dominates(*domTree, blockIndex, loop.header)) {
      return false;
    }
    return true;
  }
  if (visiting[valueId]) return false;

  int defBlockIndex = -1;
  const Instruction* def = findDefInstruction(function, valueId, defBlockIndex);
  if (def == nullptr) return false;
  switch (def->kind) {
    case InstKind::Binary:
    case InstKind::Unary:
    case InstKind::Copy:
      break;
    default:
      return false;
  }

  visiting[valueId] = 1;
  for (const auto& operand : def->operands) {
    if (!operand.isSSA()) continue;
    if (!isLoopInvariantValue(function, loop, defBlock, operand.value_id, visiting,
                              domTree)) {
      visiting[valueId] = 0;
      return false;
    }
  }
  visiting[valueId] = 0;
  return true;
}

Instruction makeBinaryInstruction(Type resultType, ir::BinaryOp op, Type operandType,
                                  int resultId, const ValueRef& lhs,
                                  const ValueRef& rhs) {
  Instruction inst;
  inst.kind = InstKind::Binary;
  inst.result_type = resultType;
  inst.result_id = resultId;
  inst.binary_op = op;
  inst.operand_type = operandType;
  inst.operands = {lhs, rhs};
  inst.has_result = true;
  return inst;
}

int firstNonPhiIndex(const BasicBlock& block) {
  int index = 0;
  while (index < static_cast<int>(block.instructions.size()) &&
         block.instructions[index].kind == InstKind::Phi) {
    ++index;
  }
  return index;
}

void rewriteAsIvPlusConstant(Instruction& inst, int baseValue, int delta) {
  if (delta == 0) {
    inst.kind = InstKind::Copy;
    inst.binary_op = ir::BinaryOp::Add;
    inst.operand_type = Type::Void();
    inst.operands = {ValueRef::SSA(baseValue, Type::I32())};
    return;
  }

  inst.kind = InstKind::Binary;
  inst.binary_op = ir::BinaryOp::Add;
  inst.operand_type = Type::I32();
  inst.operands = {ValueRef::SSA(baseValue, Type::I32()),
                   ValueRef::ImmediateInt(delta)};
}

bool simplifyDerivedUsers(Function& function, const Loop& loop,
                          const CanonicalInductionVariable& info) {
  bool changed = false;

  for (int blockIndex : loop.blocks) {
    auto& block = function.blocks[blockIndex];
    for (auto& inst : block.instructions) {
      if (!inst.has_result || inst.result_id == info.update_result) continue;

      auto rewriteFromOperand = [&](const ValueRef& operand,
                                    std::optional<int> extraDelta) {
        if (!operand.isSSA() || !extraDelta.has_value()) return false;
        if (operand.value_id != info.update_result && operand.value_id != info.phi_value) {
          return false;
        }
        const int baseDelta = operand.value_id == info.update_result ? info.step : 0;
        rewriteAsIvPlusConstant(inst, info.phi_value, baseDelta + *extraDelta);
        return true;
      };

      if (inst.kind == InstKind::Copy && inst.operands.size() == 1 &&
          inst.result_type == Type::I32() && inst.operands[0].isSSA()) {
        if (inst.operands[0].value_id == info.update_result) {
          rewriteAsIvPlusConstant(inst, info.phi_value, info.step);
          changed = true;
          continue;
        }
        if (inst.operands[0].value_id == info.phi_value) {
          rewriteAsIvPlusConstant(inst, info.phi_value, 0);
          changed = true;
          continue;
        }
      }

      if (inst.kind != InstKind::Binary || inst.operands.size() != 2 ||
          inst.result_type != Type::I32() || inst.operand_type != Type::I32()) {
        continue;
      }

      if (inst.binary_op == ir::BinaryOp::Add) {
        if (rewriteFromOperand(inst.operands[0], getConstantInt(inst.operands[1])) ||
            rewriteFromOperand(inst.operands[1], getConstantInt(inst.operands[0]))) {
          changed = true;
          continue;
        }
        continue;
      }

      if (inst.binary_op == ir::BinaryOp::Sub) {
        auto c = getConstantInt(inst.operands[1]);
        if (!c.has_value()) continue;
        if (!inst.operands[0].isSSA()) continue;
        if (inst.operands[0].value_id == info.update_result) {
          rewriteAsIvPlusConstant(inst, info.phi_value, info.step - *c);
          changed = true;
          continue;
        }
        if (inst.operands[0].value_id == info.phi_value) {
          rewriteAsIvPlusConstant(inst, info.phi_value, -*c);
          changed = true;
          continue;
        }
      }
    }
  }

  return changed;
}

MemoryBaseKey analyzeMemoryBase(const Function& function, const ValueRef& value,
                               const std::vector<int>& defBlock) {
  MemoryBaseKey key;
  switch (value.kind) {
    case ValueRef::Kind::GlobalSymbol:
      key.kind = MemoryBaseKey::Kind::Global;
      key.symbol = value.symbol;
      return key;
    case ValueRef::Kind::FrameAddress:
      key.kind = MemoryBaseKey::Kind::Frame;
      key.frame_offset = value.frame_offset;
      return key;
    case ValueRef::Kind::StackPointer:
      key.kind = MemoryBaseKey::Kind::Stack;
      return key;
    case ValueRef::Kind::SSA:
      break;
    default:
      key.kind = MemoryBaseKey::Kind::Unknown;
      return key;
  }

  int paramIndex = findPointerParamIndex(function, value.value_id);
  if (paramIndex >= 0) {
    key.kind = MemoryBaseKey::Kind::Param;
    key.param_index = paramIndex;
    return key;
  }

  if (value.value_id < 0 || value.value_id >= static_cast<int>(defBlock.size())) {
    key.kind = MemoryBaseKey::Kind::Unknown;
    return key;
  }

  int blockIndex = -1;
  const Instruction* def = findDefInstruction(function, value.value_id, blockIndex);
  if (def == nullptr || def->kind != InstKind::Binary || def->operands.size() != 2) {
    key.kind = MemoryBaseKey::Kind::Unknown;
    return key;
  }

  const ValueRef* baseOperand = nullptr;
  switch (def->binary_op) {
    case ir::BinaryOp::Add:
      if (def->operands[0].isPointerLike()) {
        baseOperand = &def->operands[0];
      } else if (def->operands[1].isPointerLike()) {
        baseOperand = &def->operands[1];
      }
      break;
    case ir::BinaryOp::Sub:
      if (def->operands[0].isPointerLike()) baseOperand = &def->operands[0];
      break;
    default:
      break;
  }

  if (baseOperand == nullptr) {
    key.kind = MemoryBaseKey::Kind::Unknown;
    return key;
  }
  return analyzeMemoryBase(function, *baseOperand, defBlock);
}

std::optional<AffineIvExpr> matchAffineIvExprImpl(
    const Function& function, const Loop& loop, const std::vector<int>& defBlock,
    const CanonicalInductionVariable& info, const DominatorTree& domTree,
    const ValueRef& value, AffineMatchCache& cache) {
  if (auto constant = getConstantInt(value); constant.has_value()) {
    return AffineIvExpr{0, ValueRef::Invalid(), 0, *constant};
  }
  if (!value.isSSA()) {
    if (isPreheaderAvailableOperand(function, loop, defBlock, domTree, value) &&
        value.type == Type::I32()) {
      return AffineIvExpr{0, value, 1, 0};
    }
    return std::nullopt;
  }
  if (value.value_id == info.phi_value) return AffineIvExpr{1, ValueRef::Invalid(), 0, 0};
  if (value.value_id < 0 || value.value_id >= static_cast<int>(cache.values.size())) {
    return std::nullopt;
  }
  if (cache.values[value.value_id].has_value()) return cache.values[value.value_id];
  if (cache.known_miss[value.value_id]) return std::nullopt;
  if (cache.visiting[value.value_id]) return std::nullopt;

  if (isPreheaderAvailableOperand(function, loop, defBlock, domTree, value) &&
      value.type == Type::I32()) {
    AffineIvExpr invariantExpr{0, value, 1, 0};
    cache.values[value.value_id] = invariantExpr;
    return invariantExpr;
  }

  int defBlockIndex = -1;
  const Instruction* def = findDefInstruction(function, value.value_id, defBlockIndex);
  if (def == nullptr || !loopContainsBlock(loop, defBlockIndex)) {
    cache.known_miss[value.value_id] = 1;
    return std::nullopt;
  }

  cache.visiting[value.value_id] = 1;
  std::optional<AffineIvExpr> result;

  if (def->kind == InstKind::Copy && def->operands.size() == 1 &&
      def->operand_type == Type::I32() && def->result_type == Type::I32()) {
    result = matchAffineIvExprImpl(function, loop, defBlock, info, domTree, def->operands[0], cache);
  } else if (def->kind == InstKind::Phi && def->result_type == Type::I32() &&
             !def->incomings.empty()) {
    std::optional<AffineIvExpr> merged;
    bool ok = true;
    for (const auto& incoming : def->incomings) {
      auto incomingExpr =
          matchAffineIvExprImpl(function, loop, defBlock, info, domTree, incoming.value, cache);
      if (!incomingExpr.has_value()) {
        ok = false;
        break;
      }
      if (!merged.has_value()) {
        merged = incomingExpr;
        continue;
      }
      if (merged->scale != incomingExpr->scale ||
          merged->invariant_scale != incomingExpr->invariant_scale ||
          merged->bias != incomingExpr->bias ||
          !sameValueRef(merged->invariant, incomingExpr->invariant)) {
        ok = false;
        break;
      }
    }
    if (ok) result = merged;
  } else if (def->kind == InstKind::Binary && def->operands.size() == 2 &&
             def->operand_type == Type::I32() && def->result_type == Type::I32()) {
    auto lhs = matchAffineIvExprImpl(function, loop, defBlock, info, domTree, def->operands[0], cache);
    auto rhs = matchAffineIvExprImpl(function, loop, defBlock, info, domTree, def->operands[1], cache);

    switch (def->binary_op) {
      case ir::BinaryOp::Add:
        if (lhs.has_value() && rhs.has_value()) {
          result = combineAffineAdd(*lhs, *rhs);
        }
        break;
      case ir::BinaryOp::Sub:
        if (lhs.has_value() && rhs.has_value()) {
          result = combineAffineSub(*lhs, *rhs);
        }
        break;
      case ir::BinaryOp::Mul:
        if (lhs.has_value() && lhs->scale == 0 && rhs.has_value()) {
          result = multiplyAffineByConstant(*rhs, lhs->bias);
        } else if (rhs.has_value() && rhs->scale == 0 && lhs.has_value()) {
          result = multiplyAffineByConstant(*lhs, rhs->bias);
        }
        break;
      default:
        break;
    }
  }

  cache.visiting[value.value_id] = 0;
  if (result.has_value()) {
    cache.values[value.value_id] = result;
  } else {
    cache.known_miss[value.value_id] = 1;
  }
  return result;
}

std::optional<AffineIvExpr> matchAffineIvExpr(const Function& function,
                                            const Loop& loop,
                                            const std::vector<int>& defBlock,
                                            const CanonicalInductionVariable& info,
                                            const DominatorTree& domTree,
                                            const ValueRef& value,
                                            AffineMatchCache& cache) {
  return matchAffineIvExprImpl(function, loop, defBlock, info, domTree, value, cache);
}

std::optional<ValueRef> collectInvariantPointerBase(
    const Function& function, const Loop& loop, const std::vector<int>& defBlock,
    const DominatorTree& domTree, const ValueRef& value, std::vector<char>& visiting) {
  if (!value.isSSA()) {
    if (value.isPointerLike()) return value;
    return std::nullopt;
  }
  if (value.value_id < 0 || value.value_id >= static_cast<int>(defBlock.size())) {
    return std::nullopt;
  }
  if (visiting[value.value_id]) return std::nullopt;

  const int blockIndex = defBlock[value.value_id];
  if (blockIndex < 0 || !loopContainsBlock(loop, blockIndex)) {
    if (value.type == Type::Ptr() &&
        (blockIndex < 0 || loop.header < 0 || dominates(domTree, blockIndex, loop.header))) {
      return value;
    }
    return std::nullopt;
  }

  if (value.type == Type::Ptr()) {
    std::vector<char> invariantVisiting(function.next_value_id, 0);
    if (isLoopInvariantValue(function, loop, defBlock, value.value_id,
                             invariantVisiting, &domTree)) {
      return value;
    }
  }

  int defBlockIndex = -1;
  const Instruction* def = findDefInstruction(function, value.value_id, defBlockIndex);
  if (def == nullptr || def->kind != InstKind::Binary || def->operands.size() != 2 ||
      def->binary_op != ir::BinaryOp::Add || def->result_type != Type::Ptr()) {
    return std::nullopt;
  }

  visiting[value.value_id] = 1;
  for (const auto& operand : def->operands) {
    auto base = collectInvariantPointerBase(function, loop, defBlock, domTree, operand, visiting);
    if (base.has_value()) {
      visiting[value.value_id] = 0;
      return base;
    }
  }
  visiting[value.value_id] = 0;
  return std::nullopt;
}

std::optional<ValueRef> getInvariantPointerBase(const Function& function, const Loop& loop,
                                                const std::vector<int>& defBlock,
                                                const DominatorTree& domTree,
                                                const ValueRef& value) {
  std::vector<char> visiting(function.next_value_id, 0);
  return collectInvariantPointerBase(function, loop, defBlock, domTree, value, visiting);
}

bool loopTreeContainsBlock(const LoopInfo& loopInfo, int loopIndex, int blockIndex) {
  if (loopIndex < 0 || loopIndex >= static_cast<int>(loopInfo.loops.size())) return false;
  if (blockIndex < 0 || blockIndex >= static_cast<int>(loopInfo.block_loop.size())) {
    return false;
  }

  int owner = loopInfo.block_loop[blockIndex];
  while (owner != -1) {
    if (owner == loopIndex) return true;
    if (owner < 0 || owner >= static_cast<int>(loopInfo.loops.size())) break;
    owner = loopInfo.loops[owner].parent;
  }
  return false;
}

bool allUsesStayInsideLoopTree(const Function& function, const LoopInfo& loopInfo,
                               int currentLoopIndex, int valueId) {
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size());
       ++blockIndex) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& inst : block.instructions) {
      for (const auto& operand : inst.operands) {
        if (!(operand.isSSA() && operand.value_id == valueId)) continue;
        if (!loopTreeContainsBlock(loopInfo, currentLoopIndex, blockIndex)) {
          return false;
        }
      }
      for (const auto& incoming : inst.incomings) {
        if (!(incoming.value.isSSA() && incoming.value.value_id == valueId)) continue;
        if (!loopTreeContainsBlock(loopInfo, currentLoopIndex, blockIndex)) {
          return false;
        }
      }
    }
  }
  return true;
}

std::optional<AddressRecurrenceCandidate> matchAddressRecurrenceCandidate(
    const Function& function, const Loop& loop, const std::vector<int>& defBlock,
    const CanonicalInductionVariable& info, const DominatorTree& domTree,
    const Instruction& inst, AffineMatchCache& affineCache) {
  if (inst.kind != InstKind::Binary || inst.binary_op != ir::BinaryOp::Add ||
      !inst.has_result || inst.result_id < 0 || inst.operands.size() != 2 ||
      inst.result_type != Type::Ptr()) {
    return std::nullopt;
  }

  for (int baseSide = 0; baseSide < 2; ++baseSide) {
    const ValueRef& base = inst.operands[baseSide];
    const ValueRef& offset = inst.operands[1 - baseSide];
    if (!base.isPointerLike()) continue;
    if (!isPreheaderAvailableOperand(function, loop, defBlock, domTree, base)) continue;
    auto invariantBase = getInvariantPointerBase(function, loop, defBlock, domTree, base);
    if (!invariantBase.has_value()) continue;
    if (!analyzeMemoryBase(function, *invariantBase, defBlock).isIdentifiable()) {
      continue;
    }
    auto affine = matchAffineIvExpr(function, loop, defBlock, info, domTree, offset, affineCache);
    if (!affine.has_value() || affine->scale == 0) continue;
    return AddressRecurrenceCandidate{inst.result_id,
                                      inst.result_type,
                                      inst.operand_type,
                                      *invariantBase,
                                      affine->invariant,
                                      affine->invariant_scale,
                                      affine->scale,
                                      affine->bias};
  }

  return std::nullopt;
}

ValueRef materializeInitialAddress(Function& function, int preheaderBlock,
                                   int& insertIndex,
                                   const AddressRecurrenceCandidate& candidate,
                                   const CanonicalInductionVariable& info) {
  ValueRef totalOffset = info.init_value;
  if (candidate.stride != 1) {
    const int scaleId = function.newValue(Type::I32());
    Instruction scaleInst =
        makeBinaryInstruction(Type::I32(), ir::BinaryOp::Mul, Type::I32(), scaleId,
                              totalOffset, ValueRef::ImmediateInt(candidate.stride));
    auto& preheaderInsts = function.blocks[preheaderBlock].instructions;
    preheaderInsts.insert(preheaderInsts.begin() + insertIndex, std::move(scaleInst));
    ++insertIndex;
    totalOffset = ValueRef::SSA(scaleId, Type::I32());
  }

  if (candidate.invariant_offset_scale != 0 && candidate.invariant_offset.isValid()) {
    ValueRef scaledInvariant = candidate.invariant_offset;
    if (candidate.invariant_offset_scale != 1) {
      const int invariantScaleId = function.newValue(Type::I32());
      Instruction invariantScaleInst = makeBinaryInstruction(
          Type::I32(), ir::BinaryOp::Mul, Type::I32(), invariantScaleId,
          candidate.invariant_offset,
          ValueRef::ImmediateInt(candidate.invariant_offset_scale));
      auto& preheaderInsts = function.blocks[preheaderBlock].instructions;
      preheaderInsts.insert(preheaderInsts.begin() + insertIndex,
                            std::move(invariantScaleInst));
      ++insertIndex;
      scaledInvariant = ValueRef::SSA(invariantScaleId, Type::I32());
    }

    const int invariantAddId = function.newValue(Type::I32());
    Instruction invariantAdd = makeBinaryInstruction(
        Type::I32(), ir::BinaryOp::Add, Type::I32(), invariantAddId, totalOffset,
        scaledInvariant);
    auto& preheaderInsts = function.blocks[preheaderBlock].instructions;
    preheaderInsts.insert(preheaderInsts.begin() + insertIndex, std::move(invariantAdd));
    ++insertIndex;
    totalOffset = ValueRef::SSA(invariantAddId, Type::I32());
  }

  if (candidate.bias != 0) {
    const int biasId = function.newValue(Type::I32());
    Instruction biasInst = makeBinaryInstruction(
        Type::I32(), ir::BinaryOp::Add, Type::I32(), biasId, totalOffset,
        ValueRef::ImmediateInt(candidate.bias));
    auto& preheaderInsts = function.blocks[preheaderBlock].instructions;
    preheaderInsts.insert(preheaderInsts.begin() + insertIndex, std::move(biasInst));
    ++insertIndex;
    totalOffset = ValueRef::SSA(biasId, Type::I32());
  }

  if (auto initInt = getConstantInt(info.init_value); initInt.has_value() &&
      candidate.invariant_offset_scale == 0) {
    const int initialDelta = (*initInt) * candidate.stride + candidate.bias;
    if (initialDelta == 0) return candidate.invariant_base;
  }

  const int initId = function.newValue(candidate.result_type);
  Instruction initAdd = makeBinaryInstruction(
      candidate.result_type, ir::BinaryOp::Add, candidate.operand_type, initId,
      candidate.invariant_base, totalOffset);
  auto& preheaderInsts = function.blocks[preheaderBlock].instructions;
  preheaderInsts.insert(preheaderInsts.begin() + insertIndex, std::move(initAdd));
  ++insertIndex;
  return ValueRef::SSA(initId, candidate.result_type);
}

ValueRef materializeStepAddress(Function& function, int latchBlock, int& insertIndex,
                                const AddressRecurrenceCandidate& candidate,
                                int phiId, int step) {
  const int delta = step * candidate.stride;
  if (delta == 0) return ValueRef::SSA(phiId, candidate.result_type);

  const int nextId = function.newValue(candidate.result_type);
  Instruction nextInst = makeBinaryInstruction(
      candidate.result_type, ir::BinaryOp::Add, candidate.operand_type, nextId,
      ValueRef::SSA(phiId, candidate.result_type), ValueRef::ImmediateInt(delta));
  auto& latchInsts = function.blocks[latchBlock].instructions;
  latchInsts.insert(latchInsts.begin() + insertIndex, std::move(nextInst));
  ++insertIndex;
  return ValueRef::SSA(nextId, candidate.result_type);
}

bool rewriteAddressRecurrences(Function& function, const Loop& loop,
                               const CanonicalInductionVariable& info,
                               const DominatorTree& domTree,
                               const LoopInfo& loopInfo) {
  const std::vector<int> defBlock = buildDefBlocks(function);
  std::vector<AddressRecurrenceCandidate> candidates;
  const int currentLoopIndex =
      loop.header >= 0 && loop.header < static_cast<int>(loopInfo.block_loop.size())
          ? loopInfo.block_loop[loop.header]
          : -1;
  AffineMatchCache affineCache;
  affineCache.values.resize(function.next_value_id);
  affineCache.visiting.assign(function.next_value_id, 0);
  affineCache.known_miss.assign(function.next_value_id, 0);

  for (int blockIndex : loop.blocks) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& inst : block.instructions) {
      auto candidate = matchAddressRecurrenceCandidate(function, loop, defBlock, info,
                                                       domTree, inst, affineCache);
      if (!candidate.has_value()) continue;
      if (!allUsesStayInsideLoopTree(function, loopInfo, currentLoopIndex,
                                     candidate->address_result)) {
        continue;
      }
      candidates.push_back(*candidate);
    }
  }

  if (candidates.empty()) return false;

  std::unordered_map<int, ValueRef> replacements;
  std::unordered_map<int, std::vector<int>> insertedLatchResults;
  int preheaderInsertIndex =
      static_cast<int>(function.blocks[info.preheader_block].instructions.size()) - 1;
  int headerInsertIndex = firstNonPhiIndex(function.blocks[info.header_block]);
  bool changed = false;

  for (const auto& candidate : candidates) {
    const ValueRef initValue = materializeInitialAddress(
        function, info.preheader_block, preheaderInsertIndex, candidate, info);

    Instruction phi;
    phi.kind = InstKind::Phi;
    phi.result_type = candidate.result_type;
    phi.result_id = function.newValue(candidate.result_type);
    phi.has_result = true;
    phi.incomings.push_back(PhiIncoming{info.preheader_block, initValue});
    for (int latchBlock : info.latch_blocks) {
      phi.incomings.push_back(PhiIncoming{latchBlock, ValueRef::Invalid()});
    }

    const int phiId = phi.result_id;
    auto& headerInsts = function.blocks[info.header_block].instructions;
    headerInsts.insert(headerInsts.begin() + headerInsertIndex, std::move(phi));
    ++headerInsertIndex;

    std::unordered_map<int, ValueRef> latchValues;
    for (int latchBlock : info.latch_blocks) {
      int latchInsertIndex =
          static_cast<int>(function.blocks[latchBlock].instructions.size()) - 1;
      ValueRef latchValue = materializeStepAddress(function, latchBlock, latchInsertIndex,
                                                   candidate, phiId, info.step);
      latchValues[latchBlock] = latchValue;
      if (latchValue.isSSA()) {
        insertedLatchResults[latchBlock].push_back(latchValue.value_id);
      }
    }

    for (auto& inst : function.blocks[info.header_block].instructions) {
      if (inst.kind != InstKind::Phi) break;
      if (inst.result_id != phiId) continue;
      for (auto& incoming : inst.incomings) {
        if (incoming.pred_block == info.preheader_block) continue;
        auto it = latchValues.find(incoming.pred_block);
        if (it != latchValues.end()) incoming.value = it->second;
      }
      break;
    }

    replacements[candidate.address_result] =
        ValueRef::SSA(phiId, candidate.result_type);
    changed = true;
  }

  if (!changed) return false;

  for (int blockIndex : loop.blocks) {
    auto& block = function.blocks[blockIndex];
    for (auto& inst : block.instructions) {
      if (inst.kind == InstKind::Phi && blockIndex == info.header_block) continue;
      if (inst.has_result) {
        auto insertedIt = insertedLatchResults.find(blockIndex);
        if (insertedIt != insertedLatchResults.end() &&
            std::find(insertedIt->second.begin(), insertedIt->second.end(),
                      inst.result_id) != insertedIt->second.end()) {
          continue;
        }
      }
      for (auto& operand : inst.operands) {
        if (!operand.isSSA()) continue;
        auto it = replacements.find(operand.value_id);
        if (it != replacements.end()) operand = it->second;
      }
      for (auto& incoming : inst.incomings) {
        if (!incoming.value.isSSA()) continue;
        auto it = replacements.find(incoming.value.value_id);
        if (it != replacements.end()) incoming.value = it->second;
      }
    }
  }

  return true;
}

}  // namespace

std::string IndVarSimplifyPass::name() const { return "indvar-simplify"; }

PassResult IndVarSimplifyPass::run(Function& function,
                                   AnalysisManager& analysisManager) {
  rebuildEdges(function);
  const LoopInfo& loopInfo = analysisManager.getLoopInfo(function);
  const DominatorTree& domTree = analysisManager.getDominatorTree(function);

  std::vector<int> order(loopInfo.loops.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
    return loopInfo.loops[lhs].depth > loopInfo.loops[rhs].depth;
  });

  bool changed = false;
  for (int loopIndex : order) {
    const Loop& loop = loopInfo.loops[loopIndex];
    auto info = matchCanonicalInductionVariable(function, loop);
    if (!info.has_value()) continue;
    bool loopChanged = false;
    loopChanged = simplifyDerivedUsers(function, loop, *info) || loopChanged;
    loopChanged =
        rewriteAddressRecurrences(function, loop, *info, domTree, loopInfo) || loopChanged;
    changed = loopChanged || changed;
  }

  if (changed) {
    rebuildEdges(function);
    normalizePhiIncomings(function);
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

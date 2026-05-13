#include "analysis.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <vector>

namespace midir {

bool MemoryLocation::isIdentifiable() const {
  return base_kind != BaseKind::Invalid && base_kind != BaseKind::Unknown;
}

bool MemoryLocation::sameBase(const MemoryLocation& other) const {
  if (base_kind != other.base_kind) return false;
  switch (base_kind) {
    case BaseKind::Global:
      return symbol == other.symbol;
    case BaseKind::Frame:
      return frame_offset == other.frame_offset;
    case BaseKind::Stack:
      return true;
    case BaseKind::Param:
      return param_index == other.param_index;
    case BaseKind::Invalid:
    case BaseKind::Unknown:
      return false;
  }
  return false;
}

int typeStoreSize(Type type) {
  switch (type.kind) {
    case TypeKind::I1:
      return 1;
    case TypeKind::I32:
    case TypeKind::F32:
      return 4;
    case TypeKind::Ptr:
      return 8;
    case TypeKind::Void:
      return 0;
  }
  return 0;
}

std::string memoryLocationKey(const MemoryLocation& location) {
  if (!location.isIdentifiable()) return {};
  std::string key = std::to_string(static_cast<int>(location.base_kind));
  key += ":";
  switch (location.base_kind) {
    case MemoryLocation::BaseKind::Global:
      key += location.symbol;
      break;
    case MemoryLocation::BaseKind::Frame:
      key += std::to_string(location.frame_offset);
      break;
    case MemoryLocation::BaseKind::Stack:
      key += "sp";
      break;
    case MemoryLocation::BaseKind::Param:
      key += std::to_string(location.param_index);
      break;
    case MemoryLocation::BaseKind::Invalid:
    case MemoryLocation::BaseKind::Unknown:
      return {};
  }
  key += ":";
  key += location.offset_known ? std::to_string(location.offset) : "?";
  key += ":";
  key += std::to_string(location.access_size);
  return key;
}

bool rangesOverlap(const MemoryLocation& lhs, const MemoryLocation& rhs) {
  if (!lhs.offset_known || !rhs.offset_known || lhs.access_size <= 0 ||
      rhs.access_size <= 0) {
    return true;
  }
  const int lhs_end = lhs.offset + lhs.access_size;
  const int rhs_end = rhs.offset + rhs.access_size;
  return lhs.offset < rhs_end && rhs.offset < lhs_end;
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
  for (int param : function.params) {
    if (param >= 0 && param < static_cast<int>(defBlock.size()) &&
        function.entry_block >= 0) {
      defBlock[param] = function.entry_block;
    }
  }
  return defBlock;
}

std::unordered_map<int, Instruction> buildInstructionDefs(const Function& function) {
  std::unordered_map<int, Instruction> defs;
  defs.reserve(function.next_value_id);
  for (const auto& block : function.blocks) {
    for (const auto& inst : block.instructions) {
      if (inst.has_result && inst.result_id >= 0) {
        defs.emplace(inst.result_id, inst);
      }
    }
  }
  return defs;
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

bool computeConstantOffset(const ValueRef& value,
                           const std::vector<int>& defBlock,
                           const std::unordered_map<int, Instruction>& defs,
                           int& offset) {
  return computeConstantOffset(value, defBlock, defs, offset, nullptr);
}

bool computeConstantOffset(const ValueRef& value,
                           const std::vector<int>& defBlock,
                           const std::unordered_map<int, Instruction>& defs,
                           int& offset,
                           MemoryAnalysisCache* cache) {
  if (value.kind == ValueRef::Kind::ImmediateInt) {
    offset = value.int_value;
    return true;
  }
  if (!value.isSSA() || value.value_id < 0 ||
      value.value_id >= static_cast<int>(defBlock.size())) {
    return false;
  }

  if (cache != nullptr && value.value_id < static_cast<int>(cache->constant_offset_known.size())) {
    if (cache->constant_offset_known[value.value_id]) {
      offset = cache->constant_offset_values[value.value_id];
      return true;
    }
    if (cache->constant_offset_in_progress[value.value_id]) {
      return false;
    }
    cache->constant_offset_in_progress[value.value_id] = 1;
  }

  auto it = defs.find(value.value_id);
  if (it == defs.end()) {
    if (cache != nullptr && value.value_id < static_cast<int>(cache->constant_offset_in_progress.size())) {
      cache->constant_offset_in_progress[value.value_id] = 0;
    }
    return false;
  }
  const Instruction& inst = it->second;
  if (!inst.has_result || inst.result_id != value.value_id ||
      inst.kind != InstKind::Binary || inst.operands.size() != 2) {
    if (cache != nullptr && value.value_id < static_cast<int>(cache->constant_offset_in_progress.size())) {
      cache->constant_offset_in_progress[value.value_id] = 0;
    }
    return false;
  }

  int lhs = 0;
  int rhs = 0;
  bool success = false;
  switch (inst.binary_op) {
    case ir::BinaryOp::Add:
      if (computeConstantOffset(inst.operands[0], defBlock, defs, lhs, cache) &&
          computeConstantOffset(inst.operands[1], defBlock, defs, rhs, cache)) {
        offset = lhs + rhs;
        success = true;
      }
      break;
    case ir::BinaryOp::Sub:
      if (computeConstantOffset(inst.operands[0], defBlock, defs, lhs, cache) &&
          computeConstantOffset(inst.operands[1], defBlock, defs, rhs, cache)) {
        offset = lhs - rhs;
        success = true;
      }
      break;
    default:
      break;
  }

  if (cache != nullptr && value.value_id < static_cast<int>(cache->constant_offset_in_progress.size())) {
    cache->constant_offset_in_progress[value.value_id] = 0;
    if (success) {
      cache->constant_offset_known[value.value_id] = 1;
      cache->constant_offset_values[value.value_id] = offset;
    }
  }
  return success;
}

MemoryLocation analyzeMemoryLocation(
    const Function& function, const ValueRef& value,
    const std::vector<int>& defBlock,
    const std::unordered_map<int, Instruction>& defs) {
  return analyzeMemoryLocation(function, value, defBlock, defs, nullptr);
}

MemoryLocation analyzeMemoryLocation(
    const Function& function, const ValueRef& value,
    const std::vector<int>& defBlock,
    const std::unordered_map<int, Instruction>& defs,
    MemoryAnalysisCache* cache) {
  MemoryLocation location;
  switch (value.kind) {
    case ValueRef::Kind::GlobalSymbol:
      location.base_kind = MemoryLocation::BaseKind::Global;
      location.symbol = value.symbol;
      location.offset_known = true;
      location.offset = 0;
      return location;
    case ValueRef::Kind::FrameAddress:
      location.base_kind = MemoryLocation::BaseKind::Frame;
      location.frame_offset = value.frame_offset;
      location.offset_known = true;
      location.offset = 0;
      return location;
    case ValueRef::Kind::StackPointer:
      location.base_kind = MemoryLocation::BaseKind::Stack;
      location.offset_known = true;
      location.offset = 0;
      return location;
    case ValueRef::Kind::SSA:
      break;
    default:
      location.base_kind = MemoryLocation::BaseKind::Unknown;
      return location;
  }

  if (cache != nullptr && value.kind == ValueRef::Kind::SSA && value.value_id >= 0 &&
      value.value_id < static_cast<int>(cache->location_known.size())) {
    if (cache->location_known[value.value_id]) {
      return cache->locations[value.value_id];
    }
    if (cache->location_in_progress[value.value_id]) {
      location.base_kind = MemoryLocation::BaseKind::Unknown;
      return location;
    }
    cache->location_in_progress[value.value_id] = 1;
  }

  int param_index = findPointerParamIndex(function, value.value_id);
  if (param_index >= 0) {
    location.base_kind = MemoryLocation::BaseKind::Param;
    location.param_index = param_index;
    location.offset_known = true;
    location.offset = 0;
    if (cache != nullptr && value.value_id < static_cast<int>(cache->location_in_progress.size())) {
      cache->location_in_progress[value.value_id] = 0;
      cache->location_known[value.value_id] = 1;
      cache->locations[value.value_id] = location;
    }
    return location;
  }

  if (value.value_id < 0 || value.value_id >= static_cast<int>(defBlock.size())) {
    location.base_kind = MemoryLocation::BaseKind::Unknown;
    if (cache != nullptr && value.value_id >= 0 &&
        value.value_id < static_cast<int>(cache->location_in_progress.size())) {
      cache->location_in_progress[value.value_id] = 0;
      cache->location_known[value.value_id] = 1;
      cache->locations[value.value_id] = location;
    }
    return location;
  }
  auto it = defs.find(value.value_id);
  if (it == defs.end()) {
    location.base_kind = MemoryLocation::BaseKind::Unknown;
    if (cache != nullptr && value.value_id < static_cast<int>(cache->location_in_progress.size())) {
      cache->location_in_progress[value.value_id] = 0;
      cache->location_known[value.value_id] = 1;
      cache->locations[value.value_id] = location;
    }
    return location;
  }

  const Instruction& inst = it->second;
  if (inst.kind != InstKind::Binary || inst.operands.size() != 2) {
    location.base_kind = MemoryLocation::BaseKind::Unknown;
    if (cache != nullptr && value.value_id < static_cast<int>(cache->location_in_progress.size())) {
      cache->location_in_progress[value.value_id] = 0;
      cache->location_known[value.value_id] = 1;
      cache->locations[value.value_id] = location;
    }
    return location;
  }

  const ValueRef* base_operand = nullptr;
  const ValueRef* offset_operand = nullptr;
  int sign = 1;
  switch (inst.binary_op) {
    case ir::BinaryOp::Add:
      if (inst.operands[0].isPointerLike()) {
        base_operand = &inst.operands[0];
        offset_operand = &inst.operands[1];
      } else if (inst.operands[1].isPointerLike()) {
        base_operand = &inst.operands[1];
        offset_operand = &inst.operands[0];
      }
      break;
    case ir::BinaryOp::Sub:
      if (inst.operands[0].isPointerLike()) {
        base_operand = &inst.operands[0];
        offset_operand = &inst.operands[1];
        sign = -1;
      }
      break;
    default:
      break;
  }
  if (base_operand == nullptr) {
    location.base_kind = MemoryLocation::BaseKind::Unknown;
    if (cache != nullptr && value.value_id < static_cast<int>(cache->location_in_progress.size())) {
      cache->location_in_progress[value.value_id] = 0;
      cache->location_known[value.value_id] = 1;
      cache->locations[value.value_id] = location;
    }
    return location;
  }

  location = analyzeMemoryLocation(function, *base_operand, defBlock, defs, cache);
  if (!location.isIdentifiable()) {
    if (cache != nullptr && value.value_id < static_cast<int>(cache->location_in_progress.size())) {
      cache->location_in_progress[value.value_id] = 0;
      cache->location_known[value.value_id] = 1;
      cache->locations[value.value_id] = location;
    }
    return location;
  }
  if (offset_operand == nullptr) {
    if (cache != nullptr && value.value_id < static_cast<int>(cache->location_in_progress.size())) {
      cache->location_in_progress[value.value_id] = 0;
      cache->location_known[value.value_id] = 1;
      cache->locations[value.value_id] = location;
    }
    return location;
  }

  int constant_offset = 0;
  if (!computeConstantOffset(*offset_operand, defBlock, defs, constant_offset, cache)) {
    location.offset_known = false;
    location.offset = 0;
    if (cache != nullptr && value.value_id < static_cast<int>(cache->location_in_progress.size())) {
      cache->location_in_progress[value.value_id] = 0;
      cache->location_known[value.value_id] = 1;
      cache->locations[value.value_id] = location;
    }
    return location;
  }

  if (!location.offset_known) {
    location.offset = sign * constant_offset;
    location.offset_known = true;
    if (cache != nullptr && value.value_id < static_cast<int>(cache->location_in_progress.size())) {
      cache->location_in_progress[value.value_id] = 0;
      cache->location_known[value.value_id] = 1;
      cache->locations[value.value_id] = location;
    }
    return location;
  }

  location.offset += sign * constant_offset;
  if (cache != nullptr && value.value_id < static_cast<int>(cache->location_in_progress.size())) {
    cache->location_in_progress[value.value_id] = 0;
    cache->location_known[value.value_id] = 1;
    cache->locations[value.value_id] = location;
  }
  return location;
}

namespace {

ValueRef resolveTrivialCopies(const Function& function, ValueRef value,
                              int& blockIndexOut,
                              const Instruction*& defInstOut);
bool loopContainsBlock(const Loop& loop, int block);
const Instruction* findDefInstruction(const Function& function, int valueId,
                                      int& blockIndexOut);

std::optional<int> getConstantInt(const ValueRef& value) {
  if (value.kind == ValueRef::Kind::ImmediateInt) return value.int_value;
  return std::nullopt;
}

std::optional<int> getConstantInt(const Function& function, ValueRef value) {
  int blockIndex = -1;
  const Instruction* defInst = nullptr;
  ValueRef resolved = resolveTrivialCopies(function, value, blockIndex, defInst);
  if (resolved.kind == ValueRef::Kind::ImmediateInt) return resolved.int_value;
  return std::nullopt;
}

bool isParameterValue(const Function& function, int valueId) {
  return std::find(function.params.begin(), function.params.end(), valueId) !=
         function.params.end();
}

bool isLoopInvariantValue(const Function& function, const Loop& loop,
                          const std::vector<int>& defBlock, int valueId,
                          std::vector<char>& visiting) {
  if (valueId < 0 || valueId >= static_cast<int>(defBlock.size())) return false;
  const int blockIndex = defBlock[valueId];
  if (blockIndex < 0) return isParameterValue(function, valueId);
  if (!loopContainsBlock(loop, blockIndex)) return true;
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
    if (!isLoopInvariantValue(function, loop, defBlock, operand.value_id, visiting)) {
      visiting[valueId] = 0;
      return false;
    }
  }
  visiting[valueId] = 0;
  return true;
}

bool isLoopInvariantOperand(const Function& function, const Loop& loop,
                            const std::vector<int>& defBlock,
                            const ValueRef& value) {
  if (!value.isSSA()) return true;
  std::vector<char> visiting(function.next_value_id, 0);
  return isLoopInvariantValue(function, loop, defBlock, value.value_id, visiting);
}

std::optional<int> foldConstantInt(const Function& function, const Loop& loop,
                                   const std::vector<int>& defBlock,
                                   const ValueRef& value,
                                   std::vector<char>& visiting) {
  auto constant = getConstantInt(function, value);
  if (constant.has_value()) return constant;
  if (!value.isSSA()) return std::nullopt;
  if (value.value_id < 0 || value.value_id >= static_cast<int>(visiting.size())) {
    return std::nullopt;
  }
  if (!isLoopInvariantOperand(function, loop, defBlock, value)) return std::nullopt;
  if (visiting[value.value_id]) return std::nullopt;

  int defBlockIndex = -1;
  const Instruction* def = findDefInstruction(function, value.value_id, defBlockIndex);
  if (def == nullptr) return std::nullopt;

  visiting[value.value_id] = 1;
  std::optional<int> result;
  if (def->kind == InstKind::Copy && def->operands.size() == 1) {
    result = foldConstantInt(function, loop, defBlock, def->operands[0], visiting);
  } else if (def->kind == InstKind::Unary && def->operands.size() == 1 &&
             def->operand_type == Type::I32() && def->result_type == Type::I32()) {
    auto operand = foldConstantInt(function, loop, defBlock, def->operands[0], visiting);
    if (operand.has_value()) {
      switch (def->unary_op) {
        case ir::UnaryOp::Plus:
          result = *operand;
          break;
        case ir::UnaryOp::Neg:
          result = -*operand;
          break;
        default:
          break;
      }
    }
  } else if (def->kind == InstKind::Binary && def->operands.size() == 2 &&
             def->operand_type == Type::I32() && def->result_type == Type::I32()) {
    auto lhs = foldConstantInt(function, loop, defBlock, def->operands[0], visiting);
    auto rhs = foldConstantInt(function, loop, defBlock, def->operands[1], visiting);
    if (lhs.has_value() && rhs.has_value()) {
      switch (def->binary_op) {
        case ir::BinaryOp::Add:
          result = *lhs + *rhs;
          break;
        case ir::BinaryOp::Sub:
          result = *lhs - *rhs;
          break;
        case ir::BinaryOp::Mul:
          result = *lhs * *rhs;
          break;
        default:
          break;
      }
    }
  }
  visiting[value.value_id] = 0;
  return result;
}

std::vector<int> collectNaturalLoop(const Function& function, int header,
                                    int latch) {
  std::vector<int> blocks;
  if (header < 0 || latch < 0 ||
      header >= static_cast<int>(function.blocks.size()) ||
      latch >= static_cast<int>(function.blocks.size())) {
    return blocks;
  }

  std::vector<bool> in_loop(function.blocks.size(), false);
  std::vector<int> stack;
  in_loop[header] = true;
  blocks.push_back(header);
  if (!in_loop[latch]) {
    in_loop[latch] = true;
    blocks.push_back(latch);
    stack.push_back(latch);
  }

  while (!stack.empty()) {
    int block = stack.back();
    stack.pop_back();
    for (int pred : function.blocks[block].preds) {
      if (pred < 0 || pred >= static_cast<int>(function.blocks.size()) ||
          in_loop[pred]) {
        continue;
      }
      in_loop[pred] = true;
      blocks.push_back(pred);
      if (pred != header) {
        stack.push_back(pred);
      }
    }
  }

  std::sort(blocks.begin(), blocks.end());
  return blocks;
}

bool loopContainsBlock(const Loop& loop, int block) {
  return std::find(loop.blocks.begin(), loop.blocks.end(), block) !=
         loop.blocks.end();
}

std::vector<int> collectExitBlocks(const Function& function, const Loop& loop) {
  std::vector<int> exits;
  for (int block : loop.blocks) {
    for (int succ : function.blocks[block].succs) {
      if (loopContainsBlock(loop, succ)) continue;
      if (std::find(exits.begin(), exits.end(), succ) == exits.end()) {
        exits.push_back(succ);
      }
    }
  }
  std::sort(exits.begin(), exits.end());
  return exits;
}

std::vector<int> collectExitingBlocks(const Function& function, const Loop& loop) {
  std::vector<int> exiting;
  for (int block : loop.blocks) {
    const auto& succs = function.blocks[block].succs;
    if (std::any_of(succs.begin(), succs.end(),
                    [&](int succ) { return !loopContainsBlock(loop, succ); })) {
      exiting.push_back(block);
    }
  }
  std::sort(exiting.begin(), exiting.end());
  return exiting;
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

ValueRef resolveTrivialCopies(const Function& function, ValueRef value,
                              int& blockIndexOut,
                              const Instruction*& defInstOut) {
  blockIndexOut = -1;
  defInstOut = nullptr;
  while (value.isSSA()) {
    int currentBlock = -1;
    const Instruction* defInst =
        findDefInstruction(function, value.value_id, currentBlock);
    if (defInst == nullptr) break;
    if (defInst->kind != InstKind::Copy || defInst->operands.size() != 1) {
      blockIndexOut = currentBlock;
      defInstOut = defInst;
      return value;
    }
    value = defInst->operands[0];
  }
  return value;
}

std::optional<int> matchInductionStep(const Instruction& updateInst, int phiValue) {
  if (updateInst.kind != InstKind::Binary || !updateInst.has_result ||
      updateInst.operands.size() != 2 || updateInst.operand_type != Type::I32() ||
      updateInst.result_type != Type::I32()) {
    return std::nullopt;
  }

  if (updateInst.binary_op == ir::BinaryOp::Add) {
    if (updateInst.operands[0].isSSA() && updateInst.operands[0].value_id == phiValue) {
      return getConstantInt(updateInst.operands[1]);
    }
    if (updateInst.operands[1].isSSA() && updateInst.operands[1].value_id == phiValue) {
      return getConstantInt(updateInst.operands[0]);
    }
    return std::nullopt;
  }

  if (updateInst.binary_op == ir::BinaryOp::Sub) {
    if (!(updateInst.operands[0].isSSA() && updateInst.operands[0].value_id == phiValue)) {
      return std::nullopt;
    }
    auto constant = getConstantInt(updateInst.operands[1]);
    if (!constant.has_value()) return std::nullopt;
    return -*constant;
  }

  return std::nullopt;
}

std::optional<int> matchInductionStepForIncoming(const Function& function,
                                                 const Loop& loop, int phiValue,
                                                 const ValueRef& incomingValue,
                                                 int incomingPred,
                                                 std::vector<char>& visiting) {
  if (!incomingValue.isSSA()) return std::nullopt;

  int updateBlock = -1;
  const Instruction* updateInst = nullptr;
  ValueRef updateValue =
      resolveTrivialCopies(function, incomingValue, updateBlock, updateInst);
  if (!updateValue.isSSA() || updateInst == nullptr || updateBlock != incomingPred) {
    return std::nullopt;
  }

  if (auto step = matchInductionStep(*updateInst, phiValue); step.has_value()) {
    return step;
  }

  if (updateInst->kind != InstKind::Phi || updateInst->result_id < 0 ||
      updateInst->result_id >= static_cast<int>(visiting.size())) {
    return std::nullopt;
  }
  if (visiting[updateInst->result_id]) return std::nullopt;

  visiting[updateInst->result_id] = 1;
  std::optional<int> mergedStep;
  for (const auto& incoming : updateInst->incomings) {
    if (!loopContainsBlock(loop, incoming.pred_block)) {
      visiting[updateInst->result_id] = 0;
      return std::nullopt;
    }
    auto step = matchInductionStepForIncoming(function, loop, phiValue,
                                              incoming.value, incoming.pred_block,
                                              visiting);
    if (!step.has_value()) {
      visiting[updateInst->result_id] = 0;
      return std::nullopt;
    }
    if (!mergedStep.has_value()) {
      mergedStep = step;
    } else if (*mergedStep != *step) {
      visiting[updateInst->result_id] = 0;
      return std::nullopt;
    }
  }
  visiting[updateInst->result_id] = 0;
  return mergedStep;
}

std::optional<int64_t> computeTripCountForPredicate(ir::BinaryOp compareOp, int64_t init,
                                                    int64_t step, int64_t bound) {
  if (step == 0) return std::nullopt;

  switch (compareOp) {
    case ir::BinaryOp::Lt:
      if (step > 0) {
        if (init >= bound) return int64_t{0};
        return ((bound - init - 1) / step) + 1;
      }
      return std::nullopt;
    case ir::BinaryOp::Le:
      if (step > 0) {
        if (init > bound) return int64_t{0};
        return ((bound - init) / step) + 1;
      }
      return std::nullopt;
    case ir::BinaryOp::Gt:
      if (step < 0) {
        const int64_t stride = -step;
        if (init <= bound) return int64_t{0};
        return ((init - bound - 1) / stride) + 1;
      }
      return std::nullopt;
    case ir::BinaryOp::Ge:
      if (step < 0) {
        const int64_t stride = -step;
        if (init < bound) return int64_t{0};
        return ((init - bound) / stride) + 1;
      }
      return std::nullopt;
    default:
      break;
  }
  return std::nullopt;
}

}  // namespace

DominatorTree buildDominatorTree(const Function& function) {
  DominatorTree tree;
  const int n = static_cast<int>(function.blocks.size());
  tree.idom.assign(n, -1);
  tree.children.assign(n, {});
  tree.frontier.assign(n, {});
  if (n == 0 || function.entry_block < 0) {
    return tree;
  }

  std::vector<int> postorder;
  postorder.reserve(n);
  std::vector<bool> visited(n, false);

  std::function<void(int)> dfs = [&](int block) {
    visited[block] = true;
    for (int succ : function.blocks[block].succs) {
      if (succ >= 0 && succ < n && !visited[succ]) {
        dfs(succ);
      }
    }
    postorder.push_back(block);
  };
  dfs(function.entry_block);

  std::vector<int> rpo(postorder.rbegin(), postorder.rend());
  std::vector<int> rpoIndex(n, -1);
  for (int i = 0; i < static_cast<int>(rpo.size()); ++i) {
    rpoIndex[rpo[i]] = i;
  }

  auto intersect = [&](int b1, int b2) {
    while (b1 != b2) {
      while (rpoIndex[b1] > rpoIndex[b2]) b1 = tree.idom[b1];
      while (rpoIndex[b2] > rpoIndex[b1]) b2 = tree.idom[b2];
    }
    return b1;
  };

  tree.idom[function.entry_block] = function.entry_block;
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 1; i < rpo.size(); ++i) {
      int block = rpo[i];
      int newIdom = -1;
      for (int pred : function.blocks[block].preds) {
        if (pred < 0 || pred >= n || tree.idom[pred] == -1) continue;
        if (newIdom == -1) {
          newIdom = pred;
        } else {
          newIdom = intersect(pred, newIdom);
        }
      }
      if (newIdom != -1 && tree.idom[block] != newIdom) {
        tree.idom[block] = newIdom;
        changed = true;
      }
    }
  }

  for (int block : rpo) {
    if (block == function.entry_block) continue;
    int parent = tree.idom[block];
    if (parent >= 0) {
      tree.children[parent].push_back(block);
    }
  }

  for (int block : rpo) {
    if (function.blocks[block].preds.size() < 2) continue;
    for (int pred : function.blocks[block].preds) {
      int runner = pred;
      while (runner != -1 && runner != tree.idom[block]) {
        auto& frontier = tree.frontier[runner];
        if (std::find(frontier.begin(), frontier.end(), block) == frontier.end()) {
          frontier.push_back(block);
        }
        if (runner == tree.idom[runner]) break;
        runner = tree.idom[runner];
      }
    }
  }

  return tree;
}

bool dominates(const DominatorTree& domTree, int dominator, int node) {
  if (dominator < 0 || node < 0 || node >= static_cast<int>(domTree.idom.size())) {
    return false;
  }
  int current = node;
  while (current >= 0 && current < static_cast<int>(domTree.idom.size())) {
    if (current == dominator) return true;
    if (domTree.idom[current] == current) break;
    current = domTree.idom[current];
  }
  return false;
}

LoopInfo buildLoopInfo(const Function& function, const DominatorTree& domTree) {
  LoopInfo info;
  info.block_loop.assign(function.blocks.size(), -1);

  std::unordered_map<int, int> loop_by_header;
  for (int tail = 0; tail < static_cast<int>(function.blocks.size()); ++tail) {
    for (int succ : function.blocks[tail].succs) {
      if (!dominates(domTree, succ, tail)) continue;
      std::vector<int> blocks = collectNaturalLoop(function, succ, tail);
      auto [it, inserted] = loop_by_header.emplace(succ, static_cast<int>(info.loops.size()));
      if (inserted) {
        Loop loop;
        loop.header = succ;
        loop.blocks = std::move(blocks);
        loop.latches.push_back(tail);
        loop.preheader = getLoopPreheader(function, loop);
        loop.exiting_blocks = collectExitingBlocks(function, loop);
        loop.exit_blocks = collectExitBlocks(function, loop);
        info.loops.push_back(std::move(loop));
        continue;
      }

      Loop& loop = info.loops[it->second];
      if (std::find(loop.latches.begin(), loop.latches.end(), tail) == loop.latches.end()) {
        loop.latches.push_back(tail);
      }
      for (int block : blocks) {
        if (!loopContainsBlock(loop, block)) {
          loop.blocks.push_back(block);
        }
      }
      std::sort(loop.blocks.begin(), loop.blocks.end());
      loop.preheader = getLoopPreheader(function, loop);
      loop.exiting_blocks = collectExitingBlocks(function, loop);
      loop.exit_blocks = collectExitBlocks(function, loop);
    }
  }

  std::vector<int> order(info.loops.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
    return info.loops[lhs].blocks.size() < info.loops[rhs].blocks.size();
  });

  for (int loop_index : order) {
    Loop& loop = info.loops[loop_index];
    for (size_t candidate = 0; candidate < info.loops.size(); ++candidate) {
      if (static_cast<int>(candidate) == loop_index) continue;
      const Loop& parent = info.loops[candidate];
      if (parent.blocks.size() <= loop.blocks.size()) continue;
      bool contained = std::all_of(loop.blocks.begin(), loop.blocks.end(), [&](int block) {
        return loopContainsBlock(parent, block);
      });
      if (!contained) continue;
      if (loop.parent == -1 ||
          info.loops[loop.parent].blocks.size() > parent.blocks.size()) {
        loop.parent = static_cast<int>(candidate);
      }
    }
  }

  for (size_t i = 0; i < info.loops.size(); ++i) {
    Loop& loop = info.loops[i];
    loop.depth = 1;
    if (loop.parent != -1) {
      info.loops[loop.parent].children.push_back(static_cast<int>(i));
    }
  }

  std::function<int(int)> assignDepth = [&](int loop_index) {
    Loop& loop = info.loops[loop_index];
    if (loop.parent == -1) return loop.depth = 1;
    return loop.depth = assignDepth(loop.parent) + 1;
  };
  for (size_t i = 0; i < info.loops.size(); ++i) {
    assignDepth(static_cast<int>(i));
  }

  std::vector<int> sorted_loops(order.rbegin(), order.rend());
  for (int loop_index : sorted_loops) {
    Loop& loop = info.loops[loop_index];
    loop.preheader = getLoopPreheader(function, loop);
    loop.exiting_blocks = collectExitingBlocks(function, loop);
    loop.exit_blocks = collectExitBlocks(function, loop);
    for (int block : loop.blocks) {
      if (block >= 0 && block < static_cast<int>(info.block_loop.size()) &&
          info.block_loop[block] == -1) {
        info.block_loop[block] = loop_index;
      }
    }
  }

  return info;
}

int getLoopPreheader(const Function& function, const Loop& loop) {
  std::vector<int> outside_preds;
  for (int pred : function.blocks[loop.header].preds) {
    if (!loopContainsBlock(loop, pred)) {
      outside_preds.push_back(pred);
    }
  }
  if (outside_preds.size() != 1) return -1;

  int preheader = outside_preds.front();
  const auto& succs = function.blocks[preheader].succs;
  if (succs.size() != 1 || succs.front() != loop.header) {
    return -1;
  }
  return preheader;
}

bool hasSingleLatch(const Loop& loop) { return loop.latches.size() == 1; }

std::vector<int> getLoopExitBlocks(const Function& function, const Loop& loop) {
  std::vector<int> exits;
  for (int block : loop.blocks) {
    for (int succ : function.blocks[block].succs) {
      if (loopContainsBlock(loop, succ)) continue;
      if (std::find(exits.begin(), exits.end(), succ) == exits.end()) {
        exits.push_back(succ);
      }
    }
  }
  return exits;
}

std::optional<CanonicalInductionVariable> matchCanonicalInductionVariable(
    const Function& function, const Loop& loop) {
  const int preheader = getLoopPreheader(function, loop);
  if (preheader < 0) return std::nullopt;
  const auto& header = function.blocks[loop.header];

  for (const auto& inst : header.instructions) {
    if (inst.kind != InstKind::Phi) break;
    if (!inst.has_result || inst.result_id < 0 || inst.result_type != Type::I32()) {
      continue;
    }

    const PhiIncoming* initIncoming = nullptr;
    std::vector<int> latchBlocks;
    int canonicalUpdateResult = -1;
    std::optional<int> canonicalStep;
    bool valid = true;
    std::vector<char> visiting(function.next_value_id, 0);

    for (const auto& incoming : inst.incomings) {
      if (incoming.pred_block == preheader) {
        if (initIncoming != nullptr) {
          valid = false;
          break;
        }
        initIncoming = &incoming;
        continue;
      }

      if (!incoming.value.isSSA()) {
        valid = false;
        break;
      }

      int updateBlock = -1;
      const Instruction* updateInst = nullptr;
      ValueRef updateValue =
          resolveTrivialCopies(function, incoming.value, updateBlock, updateInst);
      if (!updateValue.isSSA() || updateInst == nullptr ||
          updateBlock != incoming.pred_block) {
        valid = false;
        break;
      }

      auto step = matchInductionStepForIncoming(function, loop, inst.result_id,
                                                incoming.value, incoming.pred_block,
                                                visiting);
      if (!step.has_value()) {
        valid = false;
        break;
      }

      if (!canonicalStep.has_value()) {
        canonicalStep = step;
        canonicalUpdateResult = updateInst->result_id;
      } else if (*canonicalStep != *step) {
        valid = false;
        break;
      }
      latchBlocks.push_back(incoming.pred_block);
    }

    if (!valid || initIncoming == nullptr || latchBlocks.empty() ||
        !canonicalStep.has_value() || *canonicalStep == 0) {
      continue;
    }

    int canonicalLatch = latchBlocks.front();
    if (std::find(loop.latches.begin(), loop.latches.end(), canonicalLatch) ==
        loop.latches.end()) {
      canonicalLatch = loop.latches.empty() ? canonicalLatch : loop.latches.front();
    }

    return CanonicalInductionVariable{inst.result_id,        preheader,
                                      loop.header,           canonicalLatch,
                                      canonicalUpdateResult, *canonicalStep,
                                      initIncoming->value,   latchBlocks};
  }

  return std::nullopt;
}

LoopTripCountInfo analyzeLoopTripCount(const Function& function, const Loop& loop) {
  LoopTripCountInfo info;
  auto iv = matchCanonicalInductionVariable(function, loop);
  if (!iv.has_value()) return info;
  if (loop.exiting_blocks.size() != 1 || loop.exit_blocks.size() != 1) return info;

  const int exitingBlock = loop.exiting_blocks.front();
  const int exitBlock = loop.exit_blocks.front();
  if (exitingBlock < 0 || exitingBlock >= static_cast<int>(function.blocks.size()) ||
      exitBlock < 0 || exitBlock >= static_cast<int>(function.blocks.size())) {
    return info;
  }

  const auto& block = function.blocks[exitingBlock];
  if (block.instructions.empty()) return info;
  const auto& term = block.instructions.back();
  if (term.kind != InstKind::Branch || term.operands.size() != 1) return info;
  if (!term.operands[0].isSSA()) return info;

  int condDefBlock = -1;
  const Instruction* condInst = nullptr;
  ValueRef resolvedCond =
      resolveTrivialCopies(function, term.operands[0], condDefBlock, condInst);
  if (!resolvedCond.isSSA() || condInst == nullptr || condDefBlock != exitingBlock ||
      condInst->kind != InstKind::Binary || condInst->operands.size() != 2 ||
      (condInst->result_type != Type::I32() && condInst->result_type != Type::I1()) ||
      condInst->operand_type != Type::I32()) {
    return info;
  }

  ValueRef ivOperand = ValueRef::Invalid();
  ValueRef boundOperand = ValueRef::Invalid();
  bool canonicalDirection = true;
  if (condInst->operands[0].isSSA() && condInst->operands[0].value_id == iv->phi_value) {
    ivOperand = condInst->operands[0];
    boundOperand = condInst->operands[1];
  } else if (condInst->operands[1].isSSA() &&
             condInst->operands[1].value_id == iv->phi_value) {
    ivOperand = condInst->operands[1];
    boundOperand = condInst->operands[0];
    canonicalDirection = false;
  } else {
    return info;
  }

  ir::BinaryOp compareOp = condInst->binary_op;
  if (!canonicalDirection) {
    switch (compareOp) {
      case ir::BinaryOp::Lt:
        compareOp = ir::BinaryOp::Gt;
        break;
      case ir::BinaryOp::Le:
        compareOp = ir::BinaryOp::Ge;
        break;
      case ir::BinaryOp::Gt:
        compareOp = ir::BinaryOp::Lt;
        break;
      case ir::BinaryOp::Ge:
        compareOp = ir::BinaryOp::Le;
        break;
      default:
        return info;
    }
  }

  const bool continueOnTrue =
      function.block_index_by_name.at(term.true_target) != exitBlock;
  if (!continueOnTrue) {
    switch (compareOp) {
      case ir::BinaryOp::Lt:
        compareOp = ir::BinaryOp::Ge;
        break;
      case ir::BinaryOp::Le:
        compareOp = ir::BinaryOp::Gt;
        break;
      case ir::BinaryOp::Gt:
        compareOp = ir::BinaryOp::Le;
        break;
      case ir::BinaryOp::Ge:
        compareOp = ir::BinaryOp::Lt;
        break;
      default:
        return info;
    }
  }

  const auto defBlock = buildDefBlocks(function);
  std::vector<char> foldVisiting(function.next_value_id, 0);
  const auto init = foldConstantInt(function, loop, defBlock, iv->init_value, foldVisiting);
  std::fill(foldVisiting.begin(), foldVisiting.end(), 0);
  const auto bound =
      foldConstantInt(function, loop, defBlock, boundOperand, foldVisiting);

  info.is_finite = false;
  info.has_constant_trip_count = false;
  info.trip_count = 0;
  info.compare_block = exitingBlock;
  info.exiting_block = exitingBlock;
  info.exit_block = exitBlock;
  info.condition_value = condInst->result_id;
  info.bound_value = boundOperand;
  info.compare_op = compareOp;
  info.continue_on_true = continueOnTrue;

  if (!continueOnTrue) return info;
  if (!init.has_value() || !bound.has_value()) return info;

  auto tripCount = computeTripCountForPredicate(compareOp, *init, iv->step, *bound);
  if (!tripCount.has_value() || *tripCount < 0 ||
      *tripCount > std::numeric_limits<int>::max()) {
    return info;
  }

  info.is_finite = true;
  info.has_constant_trip_count = true;
  info.trip_count = static_cast<int>(*tripCount);
  return info;
}

}  // namespace midir

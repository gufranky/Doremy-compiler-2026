#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace midir {

namespace {

struct MemoryLocation {
  enum class BaseKind { Invalid, Global, Frame, Stack, Param, Unknown };

  BaseKind base_kind = BaseKind::Invalid;
  std::string symbol;
  int frame_offset = 0;
  int param_index = -1;
  bool offset_known = false;
  int offset = 0;
  int access_size = 0;

  bool isIdentifiable() const {
    return base_kind != BaseKind::Invalid && base_kind != BaseKind::Unknown;
  }

  bool sameBase(const MemoryLocation& other) const {
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
};

MemoryLocation analyzeMemoryLocation(const Function& function, const ValueRef& value,
                                     const std::vector<int>& defBlock,
                                     const std::unordered_map<int, Instruction>& defs);
bool isLoopInvariantOperand(const ValueRef& operand, const Loop& loop,
                            const std::vector<int>& defBlock,
                            const std::unordered_set<int>& hoistedValues);

bool loopContainsBlock(const Loop& loop, int block) {
  return std::binary_search(loop.blocks.begin(), loop.blocks.end(), block);
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

bool sameValueRef(const ValueRef& lhs, const ValueRef& rhs) {
  return lhs.kind == rhs.kind && lhs.type == rhs.type && lhs.value_id == rhs.value_id &&
         lhs.int_value == rhs.int_value && lhs.float_value == rhs.float_value &&
         lhs.frame_offset == rhs.frame_offset && lhs.symbol == rhs.symbol;
}

bool loopHasMemoryBarrier(const Function& function, const Loop& loop) {
  for (int blockIndex : loop.blocks) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& inst : block.instructions) {
      if (inst.kind == InstKind::Store || inst.kind == InstKind::Call) {
        return true;
      }
    }
  }
  return false;
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

bool forwardInvariantLoadsInLoop(Function& function, const Loop& loop,
                                 const std::vector<int>& defBlock,
                                 const std::unordered_map<int, Instruction>& defs) {
  if (loopHasMemoryBarrier(function, loop)) return false;

  std::unordered_map<std::string, ValueRef> availableLoads;
  bool changed = false;
  const std::unordered_set<int> noHoistedValues;
  for (int blockIndex : loop.blocks) {
    auto& block = function.blocks[blockIndex];
    for (auto it = block.instructions.begin(); it != block.instructions.end();) {
      Instruction& inst = *it;
      if (inst.kind != InstKind::Load || !inst.has_result || inst.result_id < 0 ||
          inst.operands.size() != 1) {
        ++it;
        continue;
      }
      const ValueRef& addr = inst.operands[0];
      if (!isLoopInvariantOperand(addr, loop, defBlock, noHoistedValues)) {
        ++it;
        continue;
      }
      MemoryLocation loadLocation = analyzeMemoryLocation(function, addr, defBlock, defs);
      loadLocation.access_size = typeStoreSize(inst.result_type);
      const std::string key = memoryLocationKey(loadLocation);
      if (key.empty()) {
        ++it;
        continue;
      }

      auto available = availableLoads.find(key);
      if (available == availableLoads.end()) {
        availableLoads.emplace(key, ValueRef::SSA(inst.result_id, inst.result_type));
        ++it;
        continue;
      }
      if (sameValueRef(available->second, ValueRef::SSA(inst.result_id, inst.result_type))) {
        ++it;
        continue;
      }

      replaceAllUses(function, inst.result_id, available->second);
      it = block.instructions.erase(it);
      changed = true;
    }
  }
  return changed;
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

bool computeConstantOffset(const ValueRef& value, const std::vector<int>& defBlock,
                           const std::unordered_map<int, Instruction>& defs,
                           int& offset) {
  if (value.kind == ValueRef::Kind::ImmediateInt) {
    offset = value.int_value;
    return true;
  }
  if (!value.isSSA() || value.value_id < 0 ||
      value.value_id >= static_cast<int>(defBlock.size())) {
    return false;
  }

  auto it = defs.find(value.value_id);
  if (it == defs.end()) return false;
  const Instruction& inst = it->second;
  if (!inst.has_result || inst.result_id != value.value_id ||
      inst.kind != InstKind::Binary || inst.operands.size() != 2) {
    return false;
  }

  int lhs = 0;
  int rhs = 0;
  switch (inst.binary_op) {
    case ir::BinaryOp::Add:
      if (computeConstantOffset(inst.operands[0], defBlock, defs, lhs) &&
          computeConstantOffset(inst.operands[1], defBlock, defs, rhs)) {
        offset = lhs + rhs;
        return true;
      }
      return false;
    case ir::BinaryOp::Sub:
      if (computeConstantOffset(inst.operands[0], defBlock, defs, lhs) &&
          computeConstantOffset(inst.operands[1], defBlock, defs, rhs)) {
        offset = lhs - rhs;
        return true;
      }
      return false;
    default:
      return false;
  }
}


int findPointerParamIndex(const Function& function, int value_id) {
  for (size_t i = 0; i < function.params.size(); ++i) {
    if (function.params[i] != value_id) continue;
    if (i >= function.param_types.size()) return -1;
    if (function.param_types[i] == Type::Ptr()) return static_cast<int>(i);
    if (i < function.param_is_array.size() && function.param_is_array[i]) {
      return static_cast<int>(i);
    }
    return -1;
  }
  return -1;
}

MemoryLocation analyzeMemoryLocation(const Function& function, const ValueRef& value,
                                     const std::vector<int>& defBlock,
                                     const std::unordered_map<int, Instruction>& defs) {
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

  int param_index = findPointerParamIndex(function, value.value_id);
  if (param_index >= 0) {
    location.base_kind = MemoryLocation::BaseKind::Param;
    location.param_index = param_index;
    location.offset_known = true;
    location.offset = 0;
    return location;
  }

  if (value.value_id < 0 || value.value_id >= static_cast<int>(defBlock.size())) {
    location.base_kind = MemoryLocation::BaseKind::Unknown;
    return location;
  }
  auto it = defs.find(value.value_id);
  if (it == defs.end()) {
    location.base_kind = MemoryLocation::BaseKind::Unknown;
    return location;
  }

  const Instruction& inst = it->second;
  if (inst.kind != InstKind::Binary || inst.operands.size() != 2) {
    location.base_kind = MemoryLocation::BaseKind::Unknown;
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
    return location;
  }

  location = analyzeMemoryLocation(function, *base_operand, defBlock, defs);
  if (!location.isIdentifiable()) return location;
  if (offset_operand == nullptr) return location;

  int constant_offset = 0;
  if (!computeConstantOffset(*offset_operand, defBlock, defs, constant_offset)) {
    location.offset_known = false;
    location.offset = 0;
    return location;
  }

  if (!location.offset_known) {
    location.offset = sign * constant_offset;
    location.offset_known = true;
    return location;
  }

  location.offset += sign * constant_offset;
  return location;
}

std::unordered_map<int, Instruction> buildDefs(const Function& function) {
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

bool dominatesAllLoopUses(const Function& function, const Loop& loop,
                          const DominatorTree& domTree, int valueId,
                          int hoistBlock) {
  if (valueId < 0) return false;
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size());
       ++blockIndex) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& inst : block.instructions) {
      if (inst.kind == InstKind::Phi) {
        for (const auto& incoming : inst.incomings) {
          if (!incoming.value.isSSA() || incoming.value.value_id != valueId) continue;
          if (!loopContainsBlock(loop, incoming.pred_block)) continue;
          if (incoming.pred_block != hoistBlock &&
              !dominates(domTree, hoistBlock, incoming.pred_block)) {
            return false;
          }
        }
        continue;
      }

      bool used = std::any_of(inst.operands.begin(), inst.operands.end(),
                              [&](const ValueRef& operand) {
                                return operand.isSSA() && operand.value_id == valueId;
                              });
      if (!used || !loopContainsBlock(loop, blockIndex)) continue;
      if (blockIndex != hoistBlock && !dominates(domTree, hoistBlock, blockIndex)) {
        return false;
      }
    }
  }
  return true;
}

bool isLoopInvariantOperand(const ValueRef& operand, const Loop& loop,
                            const std::vector<int>& defBlock,
                            const std::unordered_set<int>& hoistedValues) {
  if (!operand.isSSA()) return true;
  if (operand.value_id < 0 || operand.value_id >= static_cast<int>(defBlock.size())) {
    return false;
  }
  int definingBlock = defBlock[operand.value_id];
  if (definingBlock < 0) return true;
  if (!loopContainsBlock(loop, definingBlock)) return true;
  return hoistedValues.count(operand.value_id) > 0;
}

bool isHoistableInstruction(const Instruction& inst) {
  if (!inst.has_result) return false;
  if (inst.kind == InstKind::Phi) return false;
  if (!isPureComputingInstruction(inst) || hasSideEffects(inst)) return false;
  if (inst.kind == InstKind::Call || inst.kind == InstKind::Store) return false;
  return inst.kind == InstKind::Binary || inst.kind == InstKind::Unary ||
         inst.kind == InstKind::Copy || inst.kind == InstKind::Load;
}

bool isInvariantLoadSafe(const Function& function, const Loop& loop,
                         const std::vector<int>& defBlock,
                         const std::unordered_set<int>& hoistedValues,
                         const std::unordered_map<int, Instruction>& defs,
                         const Instruction& inst) {
  if (inst.kind != InstKind::Load || inst.operands.size() != 1) return false;
  const ValueRef& addr = inst.operands[0];
  if (!isLoopInvariantOperand(addr, loop, defBlock, hoistedValues)) return false;

  MemoryLocation load_location = analyzeMemoryLocation(function, addr, defBlock, defs);
  if (!load_location.isIdentifiable()) return false;
  load_location.access_size = typeStoreSize(inst.result_type);
  if (load_location.access_size <= 0) return false;

  for (int blockIndex : loop.blocks) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& other : block.instructions) {
      if (other.kind == InstKind::Store) {
        if (other.operands.size() != 2) return false;
        MemoryLocation store_location =
            analyzeMemoryLocation(function, other.operands[1], defBlock, defs);
        if (!store_location.isIdentifiable()) return false;
        store_location.access_size = typeStoreSize(other.operands[0].type);
        if (store_location.access_size <= 0) return false;
        if (!load_location.sameBase(store_location)) continue;
        if (rangesOverlap(load_location, store_location)) return false;
        continue;
      }
      if (other.kind == InstKind::Call) {
        return false;
      }
    }
  }
  return true;
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

bool hoistLoop(Function& function, const Loop& loop, const DominatorTree& domTree) {
  const int preheader = getLoopPreheader(function, loop);
  if (preheader < 0) return false;

  std::vector<int> defBlock = buildDefBlocks(function);
  std::unordered_map<int, Instruction> defs = buildDefs(function);
  std::unordered_set<int> hoistedValues;
  std::vector<Instruction> hoistedInstructions;
  bool changed = false;
  changed = forwardInvariantLoadsInLoop(function, loop, defBlock, defs) || changed;

  bool localChanged = true;
  while (localChanged) {
    localChanged = false;
    for (int blockIndex : loop.blocks) {
      auto& block = function.blocks[blockIndex];
      for (int instIndex = 0; instIndex < static_cast<int>(block.instructions.size());
           ++instIndex) {
        const auto& inst = block.instructions[instIndex];
        if (!isHoistableInstruction(inst)) continue;
        if (inst.result_id < 0 || hoistedValues.count(inst.result_id) > 0) continue;
        if (!std::all_of(inst.operands.begin(), inst.operands.end(),
                         [&](const ValueRef& operand) {
                           return isLoopInvariantOperand(operand, loop, defBlock,
                                                         hoistedValues);
                         })) {
          continue;
        }
        if (inst.kind == InstKind::Load &&
            !isInvariantLoadSafe(function, loop, defBlock, hoistedValues, defs, inst)) {
          continue;
        }
        if (!dominatesAllLoopUses(function, loop, domTree, inst.result_id, preheader)) {
          continue;
        }

        hoistedValues.insert(inst.result_id);
        hoistedInstructions.push_back(inst);
        block.instructions.erase(block.instructions.begin() + instIndex);
        --instIndex;
        localChanged = true;
        changed = true;
      }
    }
  }

  if (!changed) return false;

  auto& preheaderBlock = function.blocks[preheader];
  int terminatorIndex = static_cast<int>(preheaderBlock.instructions.size()) - 1;
  if (terminatorIndex < 0 || !isTerminator(preheaderBlock.instructions.back().kind)) {
    return false;
  }
  preheaderBlock.instructions.insert(preheaderBlock.instructions.begin() + terminatorIndex,
                                     hoistedInstructions.begin(),
                                     hoistedInstructions.end());
  return true;
}

}  // namespace

std::string LICMPass::name() const { return "licm"; }

PassResult LICMPass::run(Function& function, AnalysisManager& analysisManager) {
  rebuildEdges(function);
  const DominatorTree& domTree = analysisManager.getDominatorTree(function);
  const LoopInfo& loopInfo = analysisManager.getLoopInfo(function);

  std::vector<int> order(loopInfo.loops.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
    return loopInfo.loops[lhs].depth > loopInfo.loops[rhs].depth;
  });

  bool changed = false;
  for (int loopIndex : order) {
    changed = hoistLoop(function, loopInfo.loops[loopIndex], domTree) || changed;
  }

  if (changed) {
    rebuildEdges(function);
    normalizePhiIncomings(function);
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

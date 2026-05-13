#include "optimizer_utils.h"

#include <algorithm>
#include <stdexcept>

namespace midir {

bool isTerminator(InstKind kind) {
  return kind == InstKind::Branch || kind == InstKind::Jump ||
         kind == InstKind::Return;
}

bool isPointerType(Type type) { return type.kind == TypeKind::Ptr; }

bool isIntegerLike(Type type) {
  return type.kind == TypeKind::I1 || type.kind == TypeKind::I32;
}

bool isNumericScalar(Type type) {
  return isIntegerLike(type) || type.kind == TypeKind::F32;
}

bool isValidCopyTypes(Type dst, Type src) {
  if (dst == src) return true;
  if (isPointerType(dst) || isPointerType(src)) return false;
  return isNumericScalar(dst) && isNumericScalar(src);
}

bool isCommutativeBinaryOp(ir::BinaryOp op) {
  return op == ir::BinaryOp::Add || op == ir::BinaryOp::Mul ||
         op == ir::BinaryOp::And || op == ir::BinaryOp::Or ||
         op == ir::BinaryOp::Eq || op == ir::BinaryOp::Ne;
}

bool hasSideEffects(const Instruction& inst) {
  return inst.kind == InstKind::Store || inst.kind == InstKind::Call ||
         isTerminator(inst.kind);
}

bool isPureComputingInstruction(const Instruction& inst) {
  return inst.kind == InstKind::Binary || inst.kind == InstKind::Unary ||
         inst.kind == InstKind::Copy || inst.kind == InstKind::Phi ||
         inst.kind == InstKind::Load;
}

bool isTrackableLocation(const MemoryLocation& location) {
  return location.isIdentifiable() && location.access_size > 0;
}

std::string valueIdentityKey(const ValueRef& value) {
  switch (value.kind) {
    case ValueRef::Kind::SSA:
      return "s" + std::to_string(value.value_id) + ":" +
             std::to_string(static_cast<int>(value.type.kind));
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

std::string aliasClassKey(const MemoryLocation& location) {
  if (!isTrackableLocation(location)) return {};
  switch (location.base_kind) {
    case MemoryLocation::BaseKind::Global:
      return "g:" + location.symbol;
    case MemoryLocation::BaseKind::Frame:
      return "f:" + std::to_string(location.frame_offset);
    case MemoryLocation::BaseKind::Stack:
      return "sp";
    case MemoryLocation::BaseKind::Param:
      return "p:" + std::to_string(location.param_index);
    case MemoryLocation::BaseKind::Invalid:
    case MemoryLocation::BaseKind::Unknown:
      return {};
  }
  return {};
}

std::vector<std::string> affectedAliasClassKeys(const MemoryLocation& location) {
  std::vector<std::string> keys;
  if (!isTrackableLocation(location)) return keys;

  const std::string current = aliasClassKey(location);
  switch (location.base_kind) {
    case MemoryLocation::BaseKind::Global:
    case MemoryLocation::BaseKind::Frame:
      if (!current.empty()) keys.push_back(current);
      break;
    case MemoryLocation::BaseKind::Stack:
      keys.push_back("sp");
      break;
    case MemoryLocation::BaseKind::Param:
      keys.push_back("params");
      break;
    case MemoryLocation::BaseKind::Invalid:
    case MemoryLocation::BaseKind::Unknown:
      break;
  }
  return keys;
}

int currentAliasVersion(const std::unordered_map<std::string, int>& aliasVersions,
                        const MemoryLocation& location) {
  if (!isTrackableLocation(location)) return 0;

  const auto readVersion = [&](const std::string& key) -> int {
    auto it = aliasVersions.find(key);
    return it == aliasVersions.end() ? 0 : it->second;
  };

  switch (location.base_kind) {
    case MemoryLocation::BaseKind::Global:
    case MemoryLocation::BaseKind::Frame:
      return readVersion(aliasClassKey(location));
    case MemoryLocation::BaseKind::Stack:
      return readVersion("sp");
    case MemoryLocation::BaseKind::Param: {
      const int exact = readVersion(aliasClassKey(location));
      const int broad = readVersion("params");
      return std::max(exact, broad);
    }
    case MemoryLocation::BaseKind::Invalid:
    case MemoryLocation::BaseKind::Unknown:
      return 0;
  }
  return 0;
}

void bumpAliasVersions(std::unordered_map<std::string, int>& aliasVersions,
                       const MemoryLocation& location) {
  for (const std::string& key : affectedAliasClassKeys(location)) {
    ++aliasVersions[key];
  }
}

std::string memoryAccessKey(const ValueRef& addr, const MemoryLocation& location,
                            Type accessType) {
  if (!isTrackableLocation(location)) return {};
  std::string key = memoryLocationKey(location);
  key += ":ty=" + std::to_string(static_cast<int>(accessType.kind));
  if (!location.offset_known) {
    key += ":addr=" + valueIdentityKey(addr);
  }
  return key;
}

void validateValueRefShape(const Function& function, const ValueRef& value,
                           const std::string& context) {
  switch (value.kind) {
    case ValueRef::Kind::Invalid:
      throw std::runtime_error(context + ": invalid value ref");
    case ValueRef::Kind::Undef:
      if (value.type == Type::Void()) {
        throw std::runtime_error(context + ": undef cannot have void type");
      }
      return;
    case ValueRef::Kind::SSA:
      if (value.value_id < 0 || value.value_id >= function.next_value_id) {
        throw std::runtime_error(context + ": SSA value id out of range");
      }
      if (value.value_id >= static_cast<int>(function.value_types.size())) {
        throw std::runtime_error(context + ": SSA value type table out of range");
      }
      if (function.value_types[value.value_id] != value.type) {
        throw std::runtime_error(context + ": SSA value type mismatch");
      }
      return;
    case ValueRef::Kind::ImmediateInt:
      if (value.type != Type::I32()) {
        throw std::runtime_error(context + ": integer immediate must be i32");
      }
      return;
    case ValueRef::Kind::ImmediateFloat:
      if (value.type != Type::F32()) {
        throw std::runtime_error(context + ": float immediate must be f32");
      }
      return;
    case ValueRef::Kind::GlobalSymbol:
    case ValueRef::Kind::FrameAddress:
    case ValueRef::Kind::StackPointer:
      if (value.type != Type::Ptr()) {
        throw std::runtime_error(context + ": address-like value must be ptr");
      }
      return;
  }
}

void rebuildEdges(Function& function) {
  function.block_index_by_name.clear();
  for (size_t i = 0; i < function.blocks.size(); ++i) {
    function.block_index_by_name[function.blocks[i].name] = static_cast<int>(i);
    function.blocks[i].preds.clear();
    function.blocks[i].succs.clear();
  }

  auto addEdge = [&](int from, int to) {
    if (from < 0 || to < 0 ||
        from >= static_cast<int>(function.blocks.size()) ||
        to >= static_cast<int>(function.blocks.size())) {
      return;
    }
    auto& succs = function.blocks[from].succs;
    if (std::find(succs.begin(), succs.end(), to) == succs.end()) {
      succs.push_back(to);
    }
    auto& preds = function.blocks[to].preds;
    if (std::find(preds.begin(), preds.end(), from) == preds.end()) {
      preds.push_back(from);
    }
  };

  for (size_t i = 0; i < function.blocks.size(); ++i) {
    auto& block = function.blocks[i];
    if (block.instructions.empty()) continue;
    const Instruction& terminator = block.instructions.back();
    if (terminator.kind == InstKind::Branch) {
      auto itTrue = function.block_index_by_name.find(terminator.true_target);
      auto itFalse = function.block_index_by_name.find(terminator.false_target);
      addEdge(static_cast<int>(i),
              itTrue == function.block_index_by_name.end() ? -1 : itTrue->second);
      addEdge(static_cast<int>(i),
              itFalse == function.block_index_by_name.end() ? -1 : itFalse->second);
    } else if (terminator.kind == InstKind::Jump) {
      auto it = function.block_index_by_name.find(terminator.jump_target);
      addEdge(static_cast<int>(i),
              it == function.block_index_by_name.end() ? -1 : it->second);
    }
  }
}

bool redirectPredecessorTerminator(Function& function, int predBlock,
                                   int oldSuccBlock, int newSuccBlock) {
  if (predBlock < 0 || oldSuccBlock < 0 || newSuccBlock < 0 ||
      predBlock >= static_cast<int>(function.blocks.size()) ||
      oldSuccBlock >= static_cast<int>(function.blocks.size()) ||
      newSuccBlock >= static_cast<int>(function.blocks.size())) {
    return false;
  }
  auto& pred = function.blocks[predBlock];
  if (pred.instructions.empty()) return false;

  Instruction& term = pred.instructions.back();
  const std::string& oldName = function.blocks[oldSuccBlock].name;
  const std::string& newName = function.blocks[newSuccBlock].name;
  bool changed = false;

  if (term.kind == InstKind::Jump) {
    if (term.jump_target == oldName) {
      term.jump_target = newName;
      changed = true;
    }
    return changed;
  }

  if (term.kind != InstKind::Branch) return false;
  if (term.true_target == oldName) {
    term.true_target = newName;
    changed = true;
  }
  if (term.false_target == oldName) {
    term.false_target = newName;
    changed = true;
  }
  return changed;
}

void rewritePhiForEdgeRedirect(Function& function, int succBlock,
                               int oldPredBlock,
                               const std::vector<int>& newPredBlocks) {
  if (succBlock < 0 || succBlock >= static_cast<int>(function.blocks.size()) ||
      oldPredBlock < 0 || oldPredBlock >= static_cast<int>(function.blocks.size())) {
    return;
  }

  auto& block = function.blocks[succBlock];
  for (auto& inst : block.instructions) {
    if (inst.kind != InstKind::Phi) break;
    std::vector<PhiIncoming> rewritten;
    rewritten.reserve(inst.incomings.size() + newPredBlocks.size());
    for (const auto& incoming : inst.incomings) {
      if (incoming.pred_block != oldPredBlock) {
        rewritten.push_back(incoming);
        continue;
      }
      for (int predBlock : newPredBlocks) {
        PhiIncoming expanded = incoming;
        expanded.pred_block = predBlock;
        rewritten.push_back(std::move(expanded));
      }
    }
    inst.incomings = std::move(rewritten);
  }
}

void normalizePhiIncomings(Function& function) {
  for (auto& block : function.blocks) {
    for (auto& inst : block.instructions) {
      if (inst.kind != InstKind::Phi) break;
      std::vector<PhiIncoming> normalized;
      normalized.reserve(block.preds.size());
      for (int predBlock : block.preds) {
        auto it = std::find_if(inst.incomings.begin(), inst.incomings.end(),
                               [&](const PhiIncoming& incoming) {
                                 return incoming.pred_block == predBlock;
                               });
        if (it == inst.incomings.end()) continue;
        normalized.push_back(*it);
      }
      inst.incomings = std::move(normalized);
    }
  }
}

bool removeBlocksAndRemap(Function& function, const std::vector<bool>& removed) {
  if (removed.size() != function.blocks.size()) return false;

  bool anyRemoved = false;
  std::vector<int> remap(function.blocks.size(), -1);
  std::vector<BasicBlock> kept;
  kept.reserve(function.blocks.size());
  for (size_t i = 0; i < function.blocks.size(); ++i) {
    if (removed[i]) {
      anyRemoved = true;
      continue;
    }
    remap[i] = static_cast<int>(kept.size());
    kept.push_back(function.blocks[i]);
  }
  if (!anyRemoved) return false;

  for (auto& block : kept) {
    for (auto& inst : block.instructions) {
      if (inst.kind != InstKind::Phi) break;
      std::vector<PhiIncoming> remapped;
      remapped.reserve(inst.incomings.size());
      for (auto incoming : inst.incomings) {
        if (incoming.pred_block < 0 ||
            incoming.pred_block >= static_cast<int>(remap.size())) {
          continue;
        }
        int newPred = remap[incoming.pred_block];
        if (newPred < 0) continue;
        incoming.pred_block = newPred;
        remapped.push_back(std::move(incoming));
      }
      inst.incomings = std::move(remapped);
    }
  }

  int newEntry = -1;
  if (function.entry_block >= 0 &&
      function.entry_block < static_cast<int>(remap.size())) {
    newEntry = remap[function.entry_block];
  }

  function.blocks = std::move(kept);
  function.entry_block = function.blocks.empty() ? -1 : std::max(newEntry, 0);
  rebuildEdges(function);
  normalizePhiIncomings(function);
  return true;
}

void pruneUnreachableBlocks(Function& function) {
  if (function.entry_block < 0 || function.blocks.empty()) return;

  std::vector<bool> reachable(function.blocks.size(), false);
  std::vector<int> stack{function.entry_block};
  while (!stack.empty()) {
    int block = stack.back();
    stack.pop_back();
    if (block < 0 || block >= static_cast<int>(function.blocks.size()) ||
        reachable[block]) {
      continue;
    }
    reachable[block] = true;
    for (int succ : function.blocks[block].succs) {
      if (succ >= 0 && succ < static_cast<int>(function.blocks.size()) &&
          !reachable[succ]) {
        stack.push_back(succ);
      }
    }
  }

  std::vector<bool> removed(function.blocks.size(), false);
  for (size_t i = 0; i < reachable.size(); ++i) {
    removed[i] = !reachable[i];
  }
  removeBlocksAndRemap(function, removed);
}

Instruction makeCopyInstruction(const Instruction& original,
                                const ValueRef& replacement) {
  Instruction inst;
  inst.kind = InstKind::Copy;
  inst.result_type = original.result_type;
  inst.result_id = original.result_id;
  inst.legacy_result_id = original.legacy_result_id;
  inst.has_result = original.has_result;
  inst.operands = {replacement};
  return inst;
}

std::vector<int> buildUseCounts(const Function& function) {
  std::vector<int> uses(function.next_value_id, 0);
  for (const auto& block : function.blocks) {
    for (const auto& inst : block.instructions) {
      for (const auto& operand : inst.operands) {
        if (operand.isSSA() && operand.value_id >= 0 &&
            operand.value_id < static_cast<int>(uses.size())) {
          ++uses[operand.value_id];
        }
      }
      for (const auto& incoming : inst.incomings) {
        if (incoming.value.isSSA() && incoming.value.value_id >= 0 &&
            incoming.value.value_id < static_cast<int>(uses.size())) {
          ++uses[incoming.value.value_id];
        }
      }
    }
  }
  return uses;
}

ValueRef resolveReplacement(ValueRef value,
                            const std::vector<ValueRef>& replacements) {
  while (value.isSSA() && value.value_id >= 0 &&
         value.value_id < static_cast<int>(replacements.size()) &&
         replacements[value.value_id].isValid()) {
    ValueRef next = replacements[value.value_id];
    if (next.isSSA() && next.value_id == value.value_id) break;
    value = next;
  }
  return value;
}

void rewriteInstructionOperands(Instruction& inst,
                                const std::vector<ValueRef>& replacements) {
  for (auto& operand : inst.operands) {
    operand = resolveReplacement(operand, replacements);
  }
  for (auto& incoming : inst.incomings) {
    incoming.value = resolveReplacement(incoming.value, replacements);
  }
}

void replaceAllUses(Function& function, int valueId, const ValueRef& replacement) {
  if (valueId < 0) return;
  std::vector<ValueRef> replacements(function.next_value_id, ValueRef::Invalid());
  replacements[valueId] = replacement;
  for (auto& block : function.blocks) {
    for (auto& inst : block.instructions) {
      rewriteInstructionOperands(inst, replacements);
    }
  }
}

}  // namespace midir

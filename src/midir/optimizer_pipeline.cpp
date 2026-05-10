#include "optimizer_pipeline.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace midir {

namespace {

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

}  // namespace

const DominatorTree& AnalysisManager::getDominatorTree(Function& function) {
  auto it = dominator_trees_.find(&function);
  if (it != dominator_trees_.end()) {
    return it->second;
  }
  auto [inserted, _] = dominator_trees_.emplace(&function, buildDominatorTree(function));
  return inserted->second;
}

void AnalysisManager::invalidate(Function& function) {
  dominator_trees_.erase(&function);
}

void AnalysisManager::invalidateAll() {
  dominator_trees_.clear();
}

std::string VerifySSAPass::name() const { return "verify-ssa"; }

PassResult VerifySSAPass::run(Function& function,
                              AnalysisManager& analysisManager) {
  if (!function.in_ssa) {
    throw std::runtime_error("function is not in SSA form: " + function.name);
  }
  if (!function.blocks.empty() &&
      (function.entry_block < 0 ||
       function.entry_block >= static_cast<int>(function.blocks.size()))) {
    throw std::runtime_error("invalid entry block in function: " + function.name);
  }
  if (function.params.size() != function.param_types.size()) {
    throw std::runtime_error("parameter metadata size mismatch in function: " +
                             function.name);
  }

  const DominatorTree& domTree = analysisManager.getDominatorTree(function);
  std::vector<int> def_block(function.next_value_id, -1);
  std::vector<int> def_inst_index(function.next_value_id, -1);
  std::vector<bool> is_param(function.next_value_id, false);

  std::unordered_set<std::string> block_names;
  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size());
       ++blockIndex) {
    const auto& block = function.blocks[blockIndex];
    if (!block_names.insert(block.name).second) {
      throw std::runtime_error("duplicate block name in function: " + function.name);
    }
    auto it = function.block_index_by_name.find(block.name);
    if (it == function.block_index_by_name.end() || it->second != blockIndex) {
      throw std::runtime_error("block index map mismatch in function: " + function.name);
    }
  }

  for (size_t i = 0; i < function.params.size(); ++i) {
    int paramId = function.params[i];
    if (paramId < 0 || paramId >= function.next_value_id) {
      throw std::runtime_error("parameter SSA id out of range in function: " +
                               function.name);
    }
    if (paramId >= static_cast<int>(function.value_types.size())) {
      throw std::runtime_error("parameter SSA type table out of range in function: " +
                               function.name);
    }
    if (is_param[paramId]) {
      throw std::runtime_error("parameter SSA id duplicated in function: " +
                               function.name);
    }
    if (function.value_types[paramId] != function.param_types[i]) {
      throw std::runtime_error("parameter SSA type mismatch in function: " +
                               function.name);
    }
    is_param[paramId] = true;
  }

  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size());
       ++blockIndex) {
    const auto& block = function.blocks[blockIndex];
    if (!block.hasTerminator()) {
      throw std::runtime_error("block missing terminator: " + block.name);
    }
    for (int instIndex = 0; instIndex < static_cast<int>(block.instructions.size());
         ++instIndex) {
      const auto& inst = block.instructions[instIndex];
      if (isTerminator(inst.kind) &&
          instIndex + 1 != static_cast<int>(block.instructions.size())) {
        throw std::runtime_error("terminator must be last instruction in block: " +
                                 block.name);
      }
      if (!inst.has_result) continue;
      if (inst.result_id < 0 || inst.result_id >= function.next_value_id) {
        throw std::runtime_error("instruction result id out of range in function: " +
                                 function.name);
      }
      if (inst.result_id >= static_cast<int>(function.value_types.size())) {
        throw std::runtime_error("instruction result type table out of range in function: " +
                                 function.name);
      }
      if (function.value_types[inst.result_id] != inst.result_type) {
        throw std::runtime_error("instruction result type mismatch in function: " +
                                 function.name);
      }
      if (is_param[inst.result_id] || def_block[inst.result_id] != -1) {
        throw std::runtime_error("SSA value defined more than once in function: " +
                                 function.name);
      }
      def_block[inst.result_id] = blockIndex;
      def_inst_index[inst.result_id] = instIndex;
    }
  }

  for (int blockIndex = 0; blockIndex < static_cast<int>(function.blocks.size());
       ++blockIndex) {
    const auto& block = function.blocks[blockIndex];
    bool seenNonPhi = false;
    std::vector<bool> seenPred(block.preds.size(), false);

    for (int instIndex = 0; instIndex < static_cast<int>(block.instructions.size());
         ++instIndex) {
      const auto& inst = block.instructions[instIndex];

      if (inst.kind == InstKind::Phi) {
        if (!inst.has_result) {
          throw std::runtime_error("phi must define a result in block: " + block.name);
        }
        if (seenNonPhi) {
          throw std::runtime_error("phi must stay at block top: " + block.name);
        }
        if (inst.incomings.size() != block.preds.size()) {
          throw std::runtime_error("phi incoming count mismatch in block: " + block.name);
        }
        for (const auto& incoming : inst.incomings) {
          auto predIt =
              std::find(block.preds.begin(), block.preds.end(), incoming.pred_block);
          if (predIt == block.preds.end()) {
            throw std::runtime_error("phi incoming predecessor mismatch in block: " +
                                     block.name);
          }
          seenPred[static_cast<size_t>(predIt - block.preds.begin())] = true;
          validateValueRefShape(function, incoming.value,
                                "phi incoming in function " + function.name);
          if (incoming.value.type != inst.result_type) {
            throw std::runtime_error("phi incoming type mismatch in block: " +
                                     block.name);
          }
          if (!incoming.value.isSSA()) continue;
          const int valueId = incoming.value.value_id;
          if (is_param[valueId]) continue;
          if (def_block[valueId] < 0) {
            throw std::runtime_error("phi uses undefined SSA value in function: " +
                                     function.name);
          }
          if (def_block[valueId] != incoming.pred_block &&
              !dominates(domTree, def_block[valueId], incoming.pred_block)) {
            throw std::runtime_error(
                "phi uses non-dominating SSA value on incoming edge in function: " +
                function.name);
          }
        }
      } else {
        seenNonPhi = true;
      }

      for (const auto& operand : inst.operands) {
        validateValueRefShape(function, operand,
                              "operand in function " + function.name);
        if (!operand.isSSA()) continue;
        const int valueId = operand.value_id;
        if (is_param[valueId]) continue;
        if (def_block[valueId] < 0) {
          throw std::runtime_error("instruction uses undefined SSA value in function: " +
                                   function.name);
        }
        if (def_block[valueId] == blockIndex) {
          if (def_inst_index[valueId] >= instIndex) {
            throw std::runtime_error(
                "instruction uses SSA value before local definition in function: " +
                function.name);
          }
          continue;
        }
        if (!dominates(domTree, def_block[valueId], blockIndex)) {
          throw std::runtime_error(
              "instruction uses non-dominating SSA value in function: " +
              function.name);
        }
      }

      switch (inst.kind) {
        case InstKind::Binary: {
          if (!inst.has_result || inst.operands.size() != 2) {
            throw std::runtime_error("binary instruction shape mismatch in function: " +
                                     function.name);
          }
          bool lhsPtr = isPointerType(inst.operands[0].type);
          bool rhsPtr = isPointerType(inst.operands[1].type);
          if (lhsPtr || rhsPtr || isPointerType(inst.result_type)) {
            bool validPtrAdd = inst.binary_op == ir::BinaryOp::Add &&
                               inst.result_type == Type::Ptr() &&
                               inst.operand_type == Type::I32() &&
                               (lhsPtr != rhsPtr) &&
                               ((lhsPtr && inst.operands[1].type == Type::I32()) ||
                                (rhsPtr && inst.operands[0].type == Type::I32()));
            if (!validPtrAdd) {
              throw std::runtime_error("illegal pointer arithmetic in function: " +
                                       function.name);
            }
            break;
          }
          if (inst.operands[0].type != inst.operand_type ||
              inst.operands[1].type != inst.operand_type) {
            throw std::runtime_error("binary operand type mismatch in function: " +
                                     function.name);
          }
          switch (inst.binary_op) {
            case ir::BinaryOp::Add:
            case ir::BinaryOp::Sub:
            case ir::BinaryOp::Mul:
            case ir::BinaryOp::Div:
            case ir::BinaryOp::Mod:
              if (!isNumericScalar(inst.operand_type) ||
                  inst.result_type != inst.operand_type) {
                throw std::runtime_error("binary arithmetic type mismatch in function: " +
                                         function.name);
              }
              break;
            case ir::BinaryOp::And:
            case ir::BinaryOp::Or:
              if (!isIntegerLike(inst.operand_type) ||
                  !isIntegerLike(inst.result_type)) {
                throw std::runtime_error("binary logical type mismatch in function: " +
                                         function.name);
              }
              break;
            case ir::BinaryOp::Lt:
            case ir::BinaryOp::Gt:
            case ir::BinaryOp::Le:
            case ir::BinaryOp::Ge:
            case ir::BinaryOp::Eq:
            case ir::BinaryOp::Ne:
              if (!isNumericScalar(inst.operand_type) ||
                  !isIntegerLike(inst.result_type)) {
                throw std::runtime_error("binary compare type mismatch in function: " +
                                         function.name);
              }
              break;
          }
          break;
        }
        case InstKind::Unary:
          if (!inst.has_result || inst.operands.size() != 1) {
            throw std::runtime_error("unary instruction shape mismatch in function: " +
                                     function.name);
          }
          if (inst.operands[0].type != inst.operand_type ||
              isPointerType(inst.operand_type)) {
            throw std::runtime_error("unary operand type mismatch in function: " +
                                     function.name);
          }
          if (inst.unary_op == ir::UnaryOp::Not) {
            if (!isNumericScalar(inst.operand_type) ||
                !isIntegerLike(inst.result_type)) {
              throw std::runtime_error("unary not type mismatch in function: " +
                                       function.name);
            }
          } else {
            if (!isNumericScalar(inst.operand_type) ||
                inst.result_type != inst.operand_type) {
              throw std::runtime_error("unary arithmetic type mismatch in function: " +
                                       function.name);
            }
          }
          break;
        case InstKind::Copy:
          if (!inst.has_result || inst.operands.size() != 1) {
            throw std::runtime_error("copy instruction shape mismatch in function: " +
                                     function.name);
          }
          if (!isValidCopyTypes(inst.result_type, inst.operands[0].type)) {
            throw std::runtime_error("copy type mismatch in function: " +
                                     function.name);
          }
          break;
        case InstKind::Phi:
          if (!inst.operands.empty()) {
            throw std::runtime_error("phi must not use operand list in function: " +
                                     function.name);
          }
          break;
        case InstKind::Load:
          if (!inst.has_result || inst.operands.size() != 1) {
            throw std::runtime_error("load instruction shape mismatch in function: " +
                                     function.name);
          }
          if (!isPointerType(inst.operands[0].type) ||
              inst.result_type == Type::Void()) {
            throw std::runtime_error("load type mismatch in function: " +
                                     function.name);
          }
          break;
        case InstKind::Store:
          if (inst.has_result || inst.operands.size() != 2) {
            throw std::runtime_error("store instruction shape mismatch in function: " +
                                     function.name);
          }
          if (!isPointerType(inst.operands[1].type) ||
              inst.operands[0].type == Type::Void()) {
            throw std::runtime_error("store type mismatch in function: " +
                                     function.name);
          }
          break;
        case InstKind::Call:
          if (inst.operands.size() != inst.call_arg_types.size()) {
            throw std::runtime_error("call argument metadata mismatch in function: " +
                                     function.name);
          }
          if (inst.callee.empty()) {
            throw std::runtime_error("call without callee in function: " +
                                     function.name);
          }
          for (size_t i = 0; i < inst.operands.size(); ++i) {
            if (inst.operands[i].type != inst.call_arg_types[i]) {
              throw std::runtime_error("call argument type mismatch in function: " +
                                       function.name);
            }
          }
          if (inst.has_result && inst.result_type == Type::Void()) {
            throw std::runtime_error("call cannot define void result in function: " +
                                     function.name);
          }
          if (!inst.has_result && inst.result_id != -1) {
            throw std::runtime_error("void call must not carry result id in function: " +
                                     function.name);
          }
          break;
        case InstKind::Branch:
          if (inst.has_result || inst.operands.size() != 1) {
            throw std::runtime_error("branch instruction shape mismatch in function: " +
                                     function.name);
          }
          if (!isNumericScalar(inst.operands[0].type) ||
              function.block_index_by_name.count(inst.true_target) == 0 ||
              function.block_index_by_name.count(inst.false_target) == 0) {
            throw std::runtime_error("branch target or condition mismatch in function: " +
                                     function.name);
          }
          break;
        case InstKind::Jump:
          if (inst.has_result || !inst.operands.empty() ||
              function.block_index_by_name.count(inst.jump_target) == 0) {
            throw std::runtime_error("jump target mismatch in function: " +
                                     function.name);
          }
          break;
        case InstKind::Return:
          if (function.return_type == Type::Void()) {
            if (inst.has_value || !inst.operands.empty()) {
              throw std::runtime_error("void return must not carry value in function: " +
                                       function.name);
            }
          } else {
            if (!inst.has_value || inst.operands.size() != 1 ||
                inst.operands[0].type != function.return_type) {
              throw std::runtime_error("return type mismatch in function: " +
                                       function.name);
            }
          }
          break;
      }
    }

    if (!block.preds.empty() && !block.instructions.empty() &&
        block.instructions.front().kind == InstKind::Phi) {
      for (bool present : seenPred) {
        if (!present) {
          throw std::runtime_error("phi missing predecessor value in block: " +
                                   block.name);
        }
      }
    }
  }
  return PassResult{};
}

void PassManager::addFunctionPass(std::unique_ptr<FunctionPass> pass) {
  function_passes_.push_back(std::move(pass));
}

bool PassManager::run(Module& module) {
  bool changed = false;
  for (auto& function : module.functions) {
    for (const auto& pass : function_passes_) {
      PassResult result = pass->run(function, analysis_manager_);
      changed = changed || result.changed;
      if (result.changed) {
        analysis_manager_.invalidate(function);
      }
    }
  }
  return changed;
}

}  // namespace midir

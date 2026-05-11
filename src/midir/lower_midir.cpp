#include "lower_midir.h"

#include <utility>
#include <vector>

namespace midir {

namespace {

ir::Operand lowerValue(const ValueRef& value) {
  switch (value.kind) {
    case ValueRef::Kind::ImmediateInt:
      return ir::Operand::Imm(value.int_value);
    case ValueRef::Kind::ImmediateFloat:
      return ir::Operand::Imm(value.float_value);
    case ValueRef::Kind::Undef:
      if (value.type.kind == TypeKind::F32) {
        return ir::Operand::Imm(0.0f);
      }
      return ir::Operand::Imm(0);
    case ValueRef::Kind::SSA:
      return ir::Operand::VReg(value.value_id, toBridgeIRType(value.type));
    case ValueRef::Kind::GlobalSymbol:
      return ir::Operand::Global(value.symbol, toBridgeIRType(value.type));
    case ValueRef::Kind::FrameAddress:
      return ir::Operand::LocalVarAddr(value.frame_offset);
    case ValueRef::Kind::StackPointer:
      return ir::Operand::StackPtr();
    case ValueRef::Kind::Invalid:
      break;
  }
  return ir::Operand::Imm(0);
}

using PhiCopyList = std::vector<std::pair<int, ValueRef>>;
using PhiCopiesByEdge = std::vector<std::vector<PhiCopyList>>;

PhiCopiesByEdge collectPhiCopies(const Function& function) {
  PhiCopiesByEdge copies(function.blocks.size(),
                         std::vector<PhiCopyList>(function.blocks.size()));
  for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex) {
    const auto& block = function.blocks[blockIndex];
    for (const auto& inst : block.instructions) {
      if (inst.kind != InstKind::Phi) break;
      for (const auto& incoming : inst.incomings) {
        if (incoming.pred_block >= 0 &&
            incoming.pred_block < static_cast<int>(function.blocks.size())) {
          copies[incoming.pred_block][blockIndex].push_back(
              {inst.result_id, incoming.value});
        }
      }
    }
  }
  return copies;
}

void emitCopies(ir::IRFunction& bridgeFunction, const PhiCopyList& copies) {
  struct PendingCopy {
    int dest;
    ir::Operand src;
    ir::ValueType type;
  };

  std::vector<PendingCopy> pending;
  pending.reserve(copies.size());
  for (const auto& [dest, src] : copies) {
    pending.push_back(PendingCopy{dest, lowerValue(src), toBridgeIRType(src.type)});
  }

  auto sourceBlocked = [&](const PendingCopy& copy) {
    if (!copy.src.isVReg()) return false;
    for (const auto& other : pending) {
      if (other.dest == copy.src.vregId) return true;
    }
    return false;
  };

  while (!pending.empty()) {
    bool emitted = false;
    for (size_t i = 0; i < pending.size(); ++i) {
      if (sourceBlocked(pending[i])) continue;
      bridgeFunction.append<ir::CopyInst>(pending[i].type, pending[i].dest,
                                          pending[i].src);
      pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
      emitted = true;
      break;
    }
    if (emitted) continue;

    PendingCopy& cycle = pending.front();
    int temp = bridgeFunction.newVReg();
    bridgeFunction.append<ir::CopyInst>(cycle.type, temp, cycle.src);
    cycle.src = ir::Operand::VReg(temp, cycle.type);
  }
}

std::string ensureEdgeBlock(Function& function, int predIndex, int succIndex,
                            int& edgeBlockCounter) {
  if (predIndex < 0 || succIndex < 0 || predIndex >= static_cast<int>(function.blocks.size()) ||
      succIndex >= static_cast<int>(function.blocks.size())) {
    return succIndex >= 0 && succIndex < static_cast<int>(function.blocks.size())
               ? function.blocks[succIndex].name
               : std::string();
  }

  const std::string edgeName = function.blocks[predIndex].name + ".edge" +
                               std::to_string(edgeBlockCounter++);
  BasicBlock edgeBlock;
  edgeBlock.name = edgeName;
  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = function.blocks[succIndex].name;
  edgeBlock.instructions.push_back(std::move(jump));
  function.blocks.push_back(std::move(edgeBlock));
  return function.blocks.back().name;
}

}  // namespace

ir::IRProgram LowerMidIR::lower(const Module& module) const {
  ir::IRProgram bridgeProgram;
  for (const auto& global : module.globals) {
    bridgeProgram.globals.emplace_back(global.name, global.initial_value,
                                       global.is_const);
    bridgeProgram.globals.back().valueType = toBridgeIRType(global.value_type);
    bridgeProgram.globals.back().typedInitialValue = global.initial_value;
  }
  for (const auto& array : module.global_arrays) {
    bridgeProgram.globalArrays.emplace_back(
        array.name, array.dimensions, toBridgeIRType(array.element_type),
        array.initial_values, array.is_const);
  }

  for (const auto& sourceFunction : module.functions) {
    Function function = sourceFunction;
    int edgeBlockCounter = 0;
    PhiCopiesByEdge pendingPhiCopies = collectPhiCopies(function);
    for (size_t predIndex = 0; predIndex < function.blocks.size(); ++predIndex) {
      if (predIndex >= pendingPhiCopies.size()) break;
      if (function.blocks[predIndex].instructions.empty()) continue;
      auto& terminator = function.blocks[predIndex].instructions.back();
      if (terminator.kind == InstKind::Branch) {
        auto trueCopies = pendingPhiCopies[predIndex][function.block_index_by_name.at(terminator.true_target)];
        auto falseCopies = pendingPhiCopies[predIndex][function.block_index_by_name.at(terminator.false_target)];
        if (!trueCopies.empty()) {
          std::string edgeName = ensureEdgeBlock(function, static_cast<int>(predIndex),
                                                 function.block_index_by_name.at(terminator.true_target),
                                                 edgeBlockCounter);
          int edgeIndex = static_cast<int>(function.blocks.size()) - 1;
          if (edgeIndex >= static_cast<int>(pendingPhiCopies.size())) {
            pendingPhiCopies.resize(function.blocks.size(),
                                    std::vector<PhiCopyList>(function.blocks.size()));
            for (auto& row : pendingPhiCopies) row.resize(function.blocks.size());
          }
          pendingPhiCopies[predIndex][function.block_index_by_name.at(terminator.true_target)].clear();
          pendingPhiCopies[edgeIndex][function.block_index_by_name.at(terminator.true_target)] = std::move(trueCopies);
          terminator.true_target = edgeName;
        }
        if (!falseCopies.empty()) {
          std::string edgeName = ensureEdgeBlock(function, static_cast<int>(predIndex),
                                                 function.block_index_by_name.at(terminator.false_target),
                                                 edgeBlockCounter);
          int edgeIndex = static_cast<int>(function.blocks.size()) - 1;
          if (edgeIndex >= static_cast<int>(pendingPhiCopies.size())) {
            pendingPhiCopies.resize(function.blocks.size(),
                                    std::vector<PhiCopyList>(function.blocks.size()));
            for (auto& row : pendingPhiCopies) row.resize(function.blocks.size());
          }
          pendingPhiCopies[predIndex][function.block_index_by_name.at(terminator.false_target)].clear();
          pendingPhiCopies[edgeIndex][function.block_index_by_name.at(terminator.false_target)] = std::move(falseCopies);
          terminator.false_target = edgeName;
        }
      }
    }

    ir::IRFunction bridgeFunction(function.name);
    bridgeFunction.returnType = toBridgeIRType(function.return_type);
    bridgeFunction.localArraySize = function.local_array_size;
    bridgeFunction.nextVReg = function.next_value_id;
    bridgeFunction.params = function.params;
    for (Type type : function.param_types) {
      bridgeFunction.paramTypes.push_back(toBridgeIRType(type));
    }
    bridgeFunction.paramIsArray = function.param_is_array;

    for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex) {
      const auto& block = function.blocks[blockIndex];
      bridgeFunction.append<ir::LabelInst>(block.name);

      std::vector<Instruction> nonPhiInstructions;
      for (const auto& inst : block.instructions) {
        if (inst.kind == InstKind::Phi) continue;
        nonPhiInstructions.push_back(inst);
      }

      bool hasBranchLikeTerminator = !nonPhiInstructions.empty() &&
                                     (nonPhiInstructions.back().kind == InstKind::Jump ||
                                      nonPhiInstructions.back().kind == InstKind::Branch);
      size_t bodyLimit = hasBranchLikeTerminator ? nonPhiInstructions.size() - 1
                                                 : nonPhiInstructions.size();

      for (size_t i = 0; i < bodyLimit; ++i) {
        const auto& inst = nonPhiInstructions[i];
        switch (inst.kind) {
          case InstKind::Binary:
            bridgeFunction.append<ir::BinaryInst>(
                inst.binary_op, toBridgeIRType(inst.operand_type),
                toBridgeIRType(inst.result_type), inst.result_id,
                lowerValue(inst.operands[0]), lowerValue(inst.operands[1]));
            break;
          case InstKind::Unary:
            bridgeFunction.append<ir::UnaryInst>(
                inst.unary_op, toBridgeIRType(inst.operand_type),
                toBridgeIRType(inst.result_type), inst.result_id,
                lowerValue(inst.operands[0]));
            break;
          case InstKind::Copy:
            bridgeFunction.append<ir::CopyInst>(toBridgeIRType(inst.result_type),
                                                inst.result_id,
                                                lowerValue(inst.operands[0]));
            break;
          case InstKind::Load:
            bridgeFunction.append<ir::LoadInst>(toBridgeIRType(inst.result_type),
                                                inst.result_id,
                                                lowerValue(inst.operands[0]));
            break;
          case InstKind::Store:
            bridgeFunction.append<ir::StoreInst>(lowerValue(inst.operands[0]),
                                                 lowerValue(inst.operands[1]));
            break;
          case InstKind::Call: {
            std::vector<ir::Operand> args;
            std::vector<ir::ValueType> argTypes;
            for (const auto& operand : inst.operands) {
              args.push_back(lowerValue(operand));
              argTypes.push_back(toBridgeIRType(operand.type));
            }
            if (inst.has_result) {
              bridgeFunction.append<ir::CallInst>(
                  toBridgeIRType(inst.result_type), inst.result_id, inst.callee,
                  std::move(args), std::move(argTypes));
            } else {
              bridgeFunction.append<ir::CallInst>(inst.callee, std::move(args),
                                                  std::move(argTypes));
            }
            break;
          }
          case InstKind::Return:
            if (inst.has_value) {
              bridgeFunction.append<ir::ReturnInst>(
                  toBridgeIRType(inst.operands[0].type),
                  lowerValue(inst.operands[0]));
            } else {
              bridgeFunction.append<ir::ReturnInst>();
            }
            break;
          case InstKind::Branch:
          case InstKind::Jump:
          case InstKind::Phi:
            break;
        }
      }

      PhiCopyList copies;
      if (blockIndex < pendingPhiCopies.size()) {
        for (const auto& edgeCopies : pendingPhiCopies[blockIndex]) {
          copies.insert(copies.end(), edgeCopies.begin(), edgeCopies.end());
        }
      }
      if (!nonPhiInstructions.empty()) {
        const auto& terminator = nonPhiInstructions.back();
        if (terminator.kind == InstKind::Jump) {
          emitCopies(bridgeFunction, copies);
          bridgeFunction.append<ir::JumpInst>(terminator.jump_target);
        } else if (terminator.kind == InstKind::Branch) {
          emitCopies(bridgeFunction, copies);
          bridgeFunction.append<ir::BranchInst>(lowerValue(terminator.operands[0]),
                                                terminator.true_target,
                                                terminator.false_target);
        } else if (terminator.kind == InstKind::Return) {
          emitCopies(bridgeFunction, copies);
          if (terminator.has_value) {
            bridgeFunction.append<ir::ReturnInst>(
                toBridgeIRType(terminator.operands[0].type),
                lowerValue(terminator.operands[0]));
          } else {
            bridgeFunction.append<ir::ReturnInst>();
          }
        } else {
          emitCopies(bridgeFunction, copies);
        }
      } else {
        emitCopies(bridgeFunction, copies);
      }
    }
    bridgeProgram.functions.push_back(std::move(bridgeFunction));
  }
  return bridgeProgram;
}

}  // namespace midir

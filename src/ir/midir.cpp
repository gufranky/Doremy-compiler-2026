#include "midir.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_set>

namespace midir {

namespace {

Operand toMidOperand(const ir::Operand& op) {
  if (op.isImm()) {
    if (op.valueType == ir::ValueType::F32) {
      return Operand::ConstFloat(op.immFloatValue);
    }
    return Operand::ConstInt(op.immValue);
  }
  if (op.isVReg()) {
    return Operand::Reg(op.vregId, ToMidType(op.valueType));
  }
  if (op.isGlobal()) {
    return Operand::Global(op.globalName);
  }
  if (op.isLocalVarAddr()) {
    return Operand::Frame(op.immValue);
  }
  if (op.isStackPtr()) {
    return Operand::StackPtr();
  }
  return Operand::Invalid();
}

ir::Operand toLIROperand(const Operand& op) {
  switch (op.kind) {
    case Operand::Kind::ConstInt:
      return ir::Operand::Imm(op.intValue);
    case Operand::Kind::ConstFloat:
      return ir::Operand::Imm(op.floatValue);
    case Operand::Kind::Register:
      return ir::Operand::VReg(op.reg, ToLIRValueType(op.type));
    case Operand::Kind::Global:
      return ir::Operand::Global(op.symbol, ToLIRValueType(op.type));
    case Operand::Kind::Frame:
      return ir::Operand::LocalVarAddr(op.frameOffset);
    case Operand::Kind::StackPtr:
      return ir::Operand::StackPtr();
    case Operand::Kind::Invalid:
      break;
  }
  return ir::Operand::Imm(0);
}

void rebuildEdges(Function& function) {
  for (auto& block : function.blocks) {
    block.preds.clear();
    block.succs.clear();
  }
  function.blockIndexByName.clear();
  for (size_t i = 0; i < function.blocks.size(); ++i) {
    function.blockIndexByName[function.blocks[i].name] = static_cast<int>(i);
  }
  for (size_t i = 0; i < function.blocks.size(); ++i) {
    const auto& block = function.blocks[i];
    if (block.instructions.empty()) continue;
    const Instruction& term = block.instructions.back();
    auto addEdge = [&](int to) {
      if (to < 0) return;
      auto& succs = function.blocks[i].succs;
      if (std::find(succs.begin(), succs.end(), to) == succs.end()) {
        succs.push_back(to);
      }
      auto& preds = function.blocks[to].preds;
      if (std::find(preds.begin(), preds.end(), static_cast<int>(i)) == preds.end()) {
        preds.push_back(static_cast<int>(i));
      }
    };
    if (term.kind == InstKind::Branch) {
      auto itT = function.blockIndexByName.find(term.trueTarget);
      auto itF = function.blockIndexByName.find(term.falseTarget);
      addEdge(itT == function.blockIndexByName.end() ? -1 : itT->second);
      addEdge(itF == function.blockIndexByName.end() ? -1 : itF->second);
    } else if (term.kind == InstKind::Jump) {
      auto it = function.blockIndexByName.find(term.jumpTarget);
      addEdge(it == function.blockIndexByName.end() ? -1 : it->second);
    } else if (term.kind != InstKind::Return && i + 1 < function.blocks.size()) {
      addEdge(static_cast<int>(i + 1));
    }
  }
}

bool isTerminator(InstKind kind) {
  return kind == InstKind::Branch || kind == InstKind::Jump || kind == InstKind::Return;
}

void ensureBlockTerminator(Function& function, size_t blockIndex) {
  auto& block = function.blocks[blockIndex];
  if (!block.instructions.empty() && isTerminator(block.instructions.back().kind)) {
    return;
  }
  if (blockIndex + 1 < function.blocks.size()) {
    Instruction jump;
    jump.kind = InstKind::Jump;
    jump.type = Type::Void();
    jump.jumpTarget = function.blocks[blockIndex + 1].name;
    block.instructions.push_back(std::move(jump));
  } else {
    Instruction ret;
    ret.kind = InstKind::Return;
    ret.type = Type::Void();
    ret.hasValue = false;
    block.instructions.push_back(std::move(ret));
  }
}

void trimInstructionsAfterTerminator(BasicBlock& block) {
  for (size_t i = 0; i < block.instructions.size(); ++i) {
    if (!isTerminator(block.instructions[i].kind)) continue;
    if (i + 1 < block.instructions.size()) {
      block.instructions.erase(block.instructions.begin() + static_cast<std::ptrdiff_t>(i + 1),
                               block.instructions.end());
    }
    return;
  }
}

void removeUnreachableBlocks(Function& function) {
  if (function.blocks.empty() || function.entryBlock < 0 ||
      function.entryBlock >= static_cast<int>(function.blocks.size())) {
    return;
  }

  rebuildEdges(function);
  std::vector<int> reachable(function.blocks.size(), 0);
  std::queue<int> work;
  reachable[function.entryBlock] = 1;
  work.push(function.entryBlock);
  while (!work.empty()) {
    int cur = work.front();
    work.pop();
    for (int succ : function.blocks[cur].succs) {
      if (succ < 0 || succ >= static_cast<int>(function.blocks.size())) continue;
      if (reachable[succ]) continue;
      reachable[succ] = 1;
      work.push(succ);
    }
  }

  std::vector<BasicBlock> newBlocks;
  newBlocks.reserve(function.blocks.size());
  std::vector<int> oldToNew(function.blocks.size(), -1);
  for (size_t i = 0; i < function.blocks.size(); ++i) {
    if (!reachable[i]) continue;
    oldToNew[i] = static_cast<int>(newBlocks.size());
    newBlocks.push_back(std::move(function.blocks[i]));
  }

  for (auto& block : newBlocks) {
    for (auto& inst : block.instructions) {
      if (inst.kind != InstKind::Phi) break;
      std::vector<std::pair<int, Operand>> remapped;
      remapped.reserve(inst.incomings.size());
      for (const auto& incoming : inst.incomings) {
        if (incoming.first < 0 ||
            incoming.first >= static_cast<int>(oldToNew.size()) ||
            oldToNew[incoming.first] < 0) {
          continue;
        }
        remapped.push_back({oldToNew[incoming.first], incoming.second});
      }
      inst.incomings = std::move(remapped);
    }
  }

  function.blocks = std::move(newBlocks);
  function.entryBlock = function.entryBlock >= 0 &&
                                function.entryBlock < static_cast<int>(oldToNew.size())
                            ? oldToNew[function.entryBlock]
                            : -1;
  rebuildEdges(function);
}

void CanonicalizeCFG(Function& function) {
  for (size_t i = 0; i < function.blocks.size(); ++i) {
    trimInstructionsAfterTerminator(function.blocks[i]);
    ensureBlockTerminator(function, i);
  }
  rebuildEdges(function);
  removeUnreachableBlocks(function);
  rebuildEdges(function);
}

void rebuildModuleUseLists(Module& module) {
  for (auto& function : module.functions) {
    RebuildUseLists(function);
  }
}

long long encodeInstKey(int block, int inst) {
  return (static_cast<long long>(block) << 32) |
         static_cast<unsigned int>(inst);
}

bool isPureCallName(const std::string& name) {
  static const std::unordered_set<std::string> impure = {
      "getint", "getch", "getfloat", "getarray", "getfarray",
      "putint", "putch", "putfloat", "putarray", "putfarray",
      "starttime", "stoptime"};
  return impure.find(name) == impure.end();
}

bool isPromotableScalarType(ir::ValueType type) {
  return type == ir::ValueType::I32 || type == ir::ValueType::F32;
}

bool blockStartsWithPhi(const BasicBlock& block) {
  return !block.instructions.empty() && block.instructions.front().kind == InstKind::Phi;
}

Operand defaultValueForType(Type type) {
  switch (type.kind) {
    case TypeKind::F32:
      return Operand::ConstFloat(0.0f);
    case TypeKind::I1:
    case TypeKind::I32:
      return Operand::ConstInt(0);
    case TypeKind::Ptr:
    case TypeKind::Void:
      return Operand::Invalid();
  }
  return Operand::Invalid();
}

void rebuildFrameObjects(Function& function) {
  struct FrameUseInfo {
    bool hasDirectScalarAccess = false;
    bool hasUnsafeUse = false;
    Type type = Type::Void();
    ir::ValueType lirType = ir::ValueType::I32;
  };

  std::unordered_map<int, FrameUseInfo> scan;
  auto ensure = [&](int offset) -> FrameUseInfo& { return scan[offset]; };
  auto markUnsafeOperand = [&](const Operand& operand) {
    if (!operand.isFrame()) return;
    ensure(operand.frameOffset).hasUnsafeUse = true;
  };

  for (const auto& block : function.blocks) {
    for (const auto& inst : block.instructions) {
      if (inst.kind == InstKind::Load && !inst.operands.empty() &&
          inst.operands[0].isFrame()) {
        auto& info = ensure(inst.operands[0].frameOffset);
        if (isPromotableScalarType(inst.lirValueType)) {
          info.hasDirectScalarAccess = true;
          info.type = inst.type;
          info.lirType = inst.lirValueType;
        } else {
          info.hasUnsafeUse = true;
        }
        continue;
      }
      if (inst.kind == InstKind::Store && inst.operands.size() >= 2 &&
          inst.operands[1].isFrame()) {
        auto& info = ensure(inst.operands[1].frameOffset);
        if (isPromotableScalarType(inst.lirValueType) &&
            !inst.operands[0].isPointerLike()) {
          info.hasDirectScalarAccess = true;
          info.type = ToMidType(inst.lirValueType);
          info.lirType = inst.lirValueType;
        } else {
          info.hasUnsafeUse = true;
        }
        markUnsafeOperand(inst.operands[0]);
        continue;
      }
      for (const auto& operand : inst.operands) {
        markUnsafeOperand(operand);
      }
      for (const auto& incoming : inst.incomings) {
        markUnsafeOperand(incoming.second);
      }
    }
  }

  function.frameObjects.clear();
  for (const auto& [offset, info] : scan) {
    VariableObject object;
    object.id = function.allocateObject();
    object.frameOffset = offset;
    object.type = info.type;
    object.lirType = info.lirType;
    object.addressEscapes = info.hasUnsafeUse;
    bool inLocalArrayRegion =
        offset >= 0 && offset < function.localArraySize && function.localArraySize > 0;
    if (inLocalArrayRegion) {
      object.kind = info.hasUnsafeUse ? ObjectKind::AddressTaken : ObjectKind::Aggregate;
      object.renameable = false;
    } else {
      object.kind = info.hasDirectScalarAccess && !info.hasUnsafeUse
                        ? ObjectKind::LocalScalar
                        : (info.hasUnsafeUse ? ObjectKind::AddressTaken
                                             : ObjectKind::Aggregate);
      object.renameable = object.kind == ObjectKind::LocalScalar && !object.addressEscapes;
    }
    function.frameObjects[offset] = object;
  }

  for (auto& [offset, object] : function.frameObjects) {
    if (object.kind != ObjectKind::LocalScalar) continue;
    if (function.frameObjects.find(offset - 4) != function.frameObjects.end() ||
        function.frameObjects.find(offset + 4) != function.frameObjects.end()) {
      object.kind = ObjectKind::Aggregate;
      object.renameable = false;
      object.addressEscapes = true;
    }
  }
}

std::string makeEdgeSplitBlockName(const Function& function,
                                   const std::string& predName,
                                   const std::string& succName) {
  std::string base = predName + "_to_" + succName + "_phi";
  if (function.blockIndexByName.find(base) == function.blockIndexByName.end()) return base;
  int suffix = 0;
  while (true) {
    std::string candidate = base + "_" + std::to_string(suffix++);
    if (function.blockIndexByName.find(candidate) == function.blockIndexByName.end()) {
      return candidate;
    }
  }
}

void redirectEdgeTerminator(Instruction& term,
                            const std::string& oldTarget,
                            const std::string& newTarget) {
  if (term.kind == InstKind::Branch) {
    if (term.trueTarget == oldTarget) term.trueTarget = newTarget;
    if (term.falseTarget == oldTarget) term.falseTarget = newTarget;
  } else if (term.kind == InstKind::Jump) {
    if (term.jumpTarget == oldTarget) term.jumpTarget = newTarget;
  }
}

void splitCriticalEdgesForPhi(Function& function) {
  rebuildEdges(function);
  std::vector<std::pair<int, int>> criticalEdges;
  for (size_t succ = 0; succ < function.blocks.size(); ++succ) {
    if (!blockStartsWithPhi(function.blocks[succ])) continue;
    for (int pred : function.blocks[succ].preds) {
      if (pred < 0 || pred >= static_cast<int>(function.blocks.size())) continue;
      if (function.blocks[pred].succs.size() > 1) {
        criticalEdges.push_back({pred, static_cast<int>(succ)});
      }
    }
  }

  for (const auto& [pred, succ] : criticalEdges) {
    if (pred < 0 || succ < 0 ||
        pred >= static_cast<int>(function.blocks.size()) ||
        succ >= static_cast<int>(function.blocks.size())) {
      continue;
    }
    BasicBlock edgeBlock;
    edgeBlock.name = makeEdgeSplitBlockName(function, function.blocks[pred].name,
                                           function.blocks[succ].name);
    Instruction jump;
    jump.kind = InstKind::Jump;
    jump.type = Type::Void();
    jump.jumpTarget = function.blocks[succ].name;
    edgeBlock.instructions.push_back(std::move(jump));

    int newIndex = static_cast<int>(function.blocks.size());
    function.blocks.push_back(std::move(edgeBlock));
    function.blockIndexByName[function.blocks.back().name] = newIndex;

    Instruction& term = function.blocks[pred].instructions.back();
    redirectEdgeTerminator(term, function.blocks[succ].name,
                           function.blocks[newIndex].name);

    for (auto& phi : function.blocks[succ].instructions) {
      if (phi.kind != InstKind::Phi) break;
      for (auto& incoming : phi.incomings) {
        if (incoming.first == pred) {
          incoming.first = newIndex;
        }
      }
    }
  }

  rebuildEdges(function);
}

bool sameRegisterLocation(const Operand& operand, int reg) {
  return operand.isRegister() && operand.reg == reg;
}

struct EdgeCopy {
  int dest = -1;
  Operand src = Operand::Invalid();
  ir::ValueType destType = ir::ValueType::I32;
};

void emitParallelCopies(ir::IRFunction& lirFn, std::vector<EdgeCopy> copies) {
  copies.erase(std::remove_if(copies.begin(), copies.end(),
                              [](const EdgeCopy& copy) {
                                return copy.dest < 0 ||
                                       sameRegisterLocation(copy.src, copy.dest);
                              }),
               copies.end());
  while (!copies.empty()) {
    bool progressed = false;
    for (size_t i = 0; i < copies.size(); ++i) {
      bool blocked = false;
      for (size_t j = 0; j < copies.size(); ++j) {
        if (i == j) continue;
        if (sameRegisterLocation(copies[j].src, copies[i].dest)) {
          blocked = true;
          break;
        }
      }
      if (blocked) continue;
      lirFn.append<ir::CopyInst>(copies[i].destType, copies[i].dest,
                                 toLIROperand(copies[i].src));
      copies.erase(copies.begin() + static_cast<std::ptrdiff_t>(i));
      progressed = true;
      break;
    }
    if (progressed) continue;

    const EdgeCopy& cycle = copies.front();
    int temp = lirFn.nextVReg++;
    lirFn.append<ir::CopyInst>(cycle.destType, temp,
                               ir::Operand::VReg(cycle.dest, cycle.destType));
    Operand tempOperand = Operand::Reg(temp, ToMidType(cycle.destType));
    for (auto& copy : copies) {
      if (sameRegisterLocation(copy.src, cycle.dest)) {
        copy.src = tempOperand;
      }
    }
  }
}

}  // namespace

int Function::allocateValue(Type type) {
  int id = nextValueId++;
  if (static_cast<int>(values.size()) <= id) {
    values.resize(id + 1);
  }
  values[id].type = type;
  return id;
}

int Function::allocateObject() { return nextObjectId++; }

Type ToMidType(ir::ValueType valueType, bool forcePtr) {
  if (forcePtr) return Type::Ptr();
  return valueType == ir::ValueType::F32 ? Type::F32() : Type::I32();
}

ir::ValueType ToLIRValueType(Type type) {
  return type.kind == TypeKind::F32 ? ir::ValueType::F32 : ir::ValueType::I32;
}

Module BuildMidIR(const ir::IRProgram& lirProgram) {
  Module module;
  module.globals.reserve(lirProgram.globals.size());
  for (const auto& global : lirProgram.globals) {
    module.globals.push_back(
        GlobalVar{global.name, global.typedInitialValue, ToMidType(global.valueType), global.isConst});
  }
  module.globalArrays.reserve(lirProgram.globalArrays.size());
  for (const auto& array : lirProgram.globalArrays) {
    module.globalArrays.push_back(GlobalArray{array.name, array.dimensions,
                                              ToMidType(array.elementType),
                                              array.initialValues, array.isConst});
  }

  module.functions.reserve(lirProgram.functions.size());
  for (const auto& lirFn : lirProgram.functions) {
    Function fn;
    fn.name = lirFn.name;
    fn.returnType = ToMidType(lirFn.returnType);
    fn.localArraySize = lirFn.localArraySize;
    fn.nextValueId = lirFn.nextVReg;
    fn.values.resize(lirFn.nextVReg);

    for (size_t pi = 0; pi < lirFn.params.size(); ++pi) {
      int param = lirFn.params[pi];
      fn.params.push_back(param);
      if (static_cast<int>(fn.values.size()) <= param) fn.values.resize(param + 1);
      if (pi < lirFn.paramTypes.size()) {
        Type paramType = ToMidType(lirFn.paramTypes[pi],
                                   pi < lirFn.paramIsArray.size() && lirFn.paramIsArray[pi]);
        fn.values[param].type = paramType;
        VariableObject object;
        object.id = fn.allocateObject();
        object.baseReg = param;
        object.type = paramType;
        object.lirType = lirFn.paramTypes[pi];
        object.kind = (pi < lirFn.paramIsArray.size() && lirFn.paramIsArray[pi])
                          ? ObjectKind::AddressTaken
                          : ObjectKind::ParamScalar;
        object.renameable = object.kind == ObjectKind::ParamScalar;
        fn.registerObjects[param] = object;
      }
    }
    for (auto type : lirFn.paramTypes) {
      fn.paramTypes.push_back(ToMidType(type));
    }
    fn.paramIsArray = lirFn.paramIsArray;

    std::unordered_map<std::string, int> labelToBlock;
    std::vector<int> leaders;
    const auto& insts = lirFn.instructions;
    if (!insts.empty()) {
      leaders.push_back(0);
    }
    for (size_t i = 0; i < insts.size(); ++i) {
      if (auto* lbl = dynamic_cast<ir::LabelInst*>(insts[i].get())) {
        labelToBlock[lbl->label] = -1;
        if (i != 0) leaders.push_back(static_cast<int>(i));
      }
      if (insts[i]->kind == ir::InstKind::Branch ||
          insts[i]->kind == ir::InstKind::Jump ||
          insts[i]->kind == ir::InstKind::Return) {
        if (i + 1 < insts.size()) leaders.push_back(static_cast<int>(i + 1));
      }
    }
    std::sort(leaders.begin(), leaders.end());
    leaders.erase(std::unique(leaders.begin(), leaders.end()), leaders.end());

    for (size_t li = 0; li < leaders.size(); ++li) {
      int start = leaders[li];
      int end = (li + 1 < leaders.size()) ? (leaders[li + 1] - 1)
                                          : static_cast<int>(insts.size() - 1);
      BasicBlock block;
      if (insts[start]->kind == ir::InstKind::Label) {
        block.name = static_cast<ir::LabelInst*>(insts[start].get())->label;
      } else {
        block.name = "bb" + std::to_string(fn.blocks.size());
      }

      for (int i = start; i <= end; ++i) {
        ir::Instruction* lirInst = insts[i].get();
        if (lirInst->kind == ir::InstKind::Label) {
          continue;
        }
        Instruction midInst;
        switch (lirInst->kind) {
          case ir::InstKind::Binary: {
            auto* inst = static_cast<ir::BinaryInst*>(lirInst);
            midInst.kind = InstKind::Binary;
            midInst.binaryOp = inst->op;
            midInst.dest = inst->dest;
            midInst.hasResult = true;
            Operand lhs = toMidOperand(inst->lhs);
            Operand rhs = toMidOperand(inst->rhs);
            bool ptrLike = lhs.isPointerLike() || rhs.isPointerLike();
            midInst.type = ToMidType(inst->resultType, ptrLike);
            midInst.operandType = ToMidType(inst->operandType, ptrLike);
            midInst.lirOperandType = inst->operandType;
            midInst.lirResultType = inst->resultType;
            midInst.operands = {lhs, rhs};
            break;
          }
          case ir::InstKind::Unary: {
            auto* inst = static_cast<ir::UnaryInst*>(lirInst);
            midInst.kind = InstKind::Unary;
            midInst.unaryOp = inst->op;
            midInst.dest = inst->dest;
            midInst.hasResult = true;
            Operand operand = toMidOperand(inst->operand);
            bool ptrLike = operand.isPointerLike();
            midInst.type = ToMidType(inst->resultType, ptrLike);
            midInst.operandType = ToMidType(inst->operandType, ptrLike);
            midInst.lirOperandType = inst->operandType;
            midInst.lirResultType = inst->resultType;
            midInst.operands = {operand};
            break;
          }
          case ir::InstKind::Copy: {
            auto* inst = static_cast<ir::CopyInst*>(lirInst);
            midInst.kind = InstKind::Copy;
            midInst.dest = inst->dest;
            midInst.hasResult = true;
            Operand src = toMidOperand(inst->src);
            bool ptrLike = src.isPointerLike();
            midInst.type = ToMidType(inst->destType, ptrLike);
            midInst.operandType = src.type;
            midInst.lirValueType = inst->destType;
            midInst.operands = {src};
            break;
          }
          case ir::InstKind::Load: {
            auto* inst = static_cast<ir::LoadInst*>(lirInst);
            midInst.kind = InstKind::Load;
            midInst.dest = inst->dest;
            midInst.hasResult = true;
            midInst.type = ToMidType(inst->valueType);
            midInst.lirValueType = inst->valueType;
            midInst.operands = {toMidOperand(inst->addr)};
            break;
          }
          case ir::InstKind::Store: {
            auto* inst = static_cast<ir::StoreInst*>(lirInst);
            midInst.kind = InstKind::Store;
            midInst.type = Type::Void();
            midInst.lirValueType = inst->valueType;
            midInst.operands = {toMidOperand(inst->src), toMidOperand(inst->addr)};
            break;
          }
          case ir::InstKind::Branch: {
            auto* inst = static_cast<ir::BranchInst*>(lirInst);
            midInst.kind = InstKind::Branch;
            midInst.type = Type::Void();
            midInst.operands = {toMidOperand(inst->cond)};
            midInst.trueTarget = inst->trueLabel;
            midInst.falseTarget = inst->falseLabel;
            break;
          }
          case ir::InstKind::Jump: {
            auto* inst = static_cast<ir::JumpInst*>(lirInst);
            midInst.kind = InstKind::Jump;
            midInst.type = Type::Void();
            midInst.jumpTarget = inst->target;
            break;
          }
          case ir::InstKind::Call: {
            auto* inst = static_cast<ir::CallInst*>(lirInst);
            midInst.kind = InstKind::Call;
            midInst.type = ToMidType(inst->resultType);
            midInst.dest = inst->dest;
            midInst.hasResult = inst->hasDest;
            midInst.symbol = inst->callee;
            midInst.isPureCall = isPureCallName(inst->callee);
            midInst.lirResultType = inst->resultType;
            for (const auto& arg : inst->args) {
              midInst.operands.push_back(toMidOperand(arg));
            }
            for (auto argType : inst->argTypes) {
              midInst.callArgTypes.push_back(ToMidType(argType));
            }
            break;
          }
          case ir::InstKind::Return: {
            auto* inst = static_cast<ir::ReturnInst*>(lirInst);
            midInst.kind = InstKind::Return;
            midInst.type = Type::Void();
            midInst.hasValue = inst->hasValue;
            midInst.lirValueType = inst->valueType;
            if (inst->hasValue) midInst.operands.push_back(toMidOperand(inst->value));
            break;
          }
          case ir::InstKind::Label:
            continue;
        }
        block.instructions.push_back(std::move(midInst));
      }
      fn.blockIndexByName[block.name] = static_cast<int>(fn.blocks.size());
      fn.blocks.push_back(std::move(block));
    }

    fn.entryBlock = fn.blocks.empty() ? -1 : 0;
    CanonicalizeCFG(fn);
    rebuildEdges(fn);
    rebuildFrameObjects(fn);
    for (const auto& block : fn.blocks) {
      for (const auto& inst : block.instructions) {
        if (!inst.hasResult || inst.dest < 0) continue;
        if (fn.registerObjects.find(inst.dest) != fn.registerObjects.end()) continue;
        VariableObject object;
        object.id = fn.allocateObject();
        object.baseReg = inst.dest;
        object.type = inst.type;
        object.lirType = ToLIRValueType(inst.type);
        object.kind = ObjectKind::TempScalar;
        object.renameable = false;
        fn.registerObjects[inst.dest] = object;
      }
    }
    RebuildUseLists(fn);
    module.functions.push_back(std::move(fn));
  }

  rebuildModuleUseLists(module);
  return module;
}

ir::IRProgram LowerMidToLIR(const Module& module) {
  ir::IRProgram program;
  for (const auto& global : module.globals) {
    program.globals.emplace_back(global.name, global.initialValue, global.isConst);
    program.globals.back().valueType = ToLIRValueType(global.type);
    program.globals.back().typedInitialValue = global.initialValue;
  }
  for (const auto& array : module.globalArrays) {
    program.globalArrays.emplace_back(array.name, array.dimensions,
                                      ToLIRValueType(array.elementType),
                                      array.initialValues, array.isConst);
  }

  for (const auto& midFn : module.functions) {
    Function loweredFn = midFn;
    CanonicalizeCFG(loweredFn);
    splitCriticalEdgesForPhi(loweredFn);

    ir::IRFunction lirFn(midFn.name);
    lirFn.returnType = ToLIRValueType(loweredFn.returnType);
    lirFn.localArraySize = loweredFn.localArraySize;
    lirFn.nextVReg = loweredFn.nextValueId;
    lirFn.params = loweredFn.params;
    lirFn.paramIsArray = loweredFn.paramIsArray;
    for (const auto& type : loweredFn.paramTypes) {
      lirFn.paramTypes.push_back(ToLIRValueType(type));
    }

    std::vector<std::vector<EdgeCopy>> edgeCopies(loweredFn.blocks.size());
    for (size_t bi = 0; bi < loweredFn.blocks.size(); ++bi) {
      const auto& block = loweredFn.blocks[bi];
      for (const auto& inst : block.instructions) {
        if (inst.kind != InstKind::Phi) break;
        for (const auto& incoming : inst.incomings) {
          if (incoming.first < 0 ||
              incoming.first >= static_cast<int>(loweredFn.blocks.size())) {
            continue;
          }
          edgeCopies[incoming.first].push_back(
              EdgeCopy{inst.dest, incoming.second, ToLIRValueType(inst.type)});
        }
      }
    }

    for (size_t bi = 0; bi < loweredFn.blocks.size(); ++bi) {
      const auto& block = loweredFn.blocks[bi];
      lirFn.append<ir::LabelInst>(block.name);
      size_t instIndex = 0;
      while (instIndex < block.instructions.size() &&
             block.instructions[instIndex].kind == InstKind::Phi) {
        ++instIndex;
      }
      for (; instIndex < block.instructions.size(); ++instIndex) {
        const auto& inst = block.instructions[instIndex];
        bool isTerminator = inst.kind == InstKind::Branch ||
                            inst.kind == InstKind::Jump ||
                            inst.kind == InstKind::Return;
        if (isTerminator && !edgeCopies[bi].empty()) {
          emitParallelCopies(lirFn, edgeCopies[bi]);
        }
        switch (inst.kind) {
          case InstKind::Binary:
            lirFn.append<ir::BinaryInst>(inst.binaryOp, inst.lirOperandType,
                                         inst.lirResultType, inst.dest,
                                         toLIROperand(inst.operands[0]),
                                         toLIROperand(inst.operands[1]));
            break;
          case InstKind::Unary:
            lirFn.append<ir::UnaryInst>(inst.unaryOp, inst.lirOperandType,
                                        inst.lirResultType, inst.dest,
                                        toLIROperand(inst.operands[0]));
            break;
          case InstKind::Copy:
            lirFn.append<ir::CopyInst>(inst.lirValueType, inst.dest,
                                       toLIROperand(inst.operands[0]));
            break;
          case InstKind::Phi:
            break;
          case InstKind::Load:
            lirFn.append<ir::LoadInst>(inst.lirValueType, inst.dest,
                                       toLIROperand(inst.operands[0]));
            break;
          case InstKind::Store:
            lirFn.append<ir::StoreInst>(inst.lirValueType,
                                        toLIROperand(inst.operands[0]),
                                        toLIROperand(inst.operands[1]));
            break;
          case InstKind::FrameAddr:
            lirFn.append<ir::CopyInst>(ir::ValueType::I32, inst.dest,
                                       ir::Operand::LocalVarAddr(inst.frameOffset));
            break;
          case InstKind::GlobalAddr:
            lirFn.append<ir::CopyInst>(ir::ValueType::I32, inst.dest,
                                       ir::Operand::Global(inst.symbol));
            break;
          case InstKind::GEP:
            lirFn.append<ir::BinaryInst>(ir::BinaryOp::Add, ir::ValueType::I32,
                                         ir::ValueType::I32, inst.dest,
                                         toLIROperand(inst.operands[0]),
                                         ir::Operand::Imm(inst.gepOffset));
            break;
          case InstKind::Select:
            if (inst.operands.size() >= 3) {
              lirFn.append<ir::CopyInst>(ToLIRValueType(inst.type), inst.dest,
                                         toLIROperand(inst.operands[1]));
            }
            break;
          case InstKind::Call: {
            std::vector<ir::Operand> args;
            std::vector<ir::ValueType> argTypes;
            for (const auto& operand : inst.operands) {
              args.push_back(toLIROperand(operand));
            }
            for (const auto& argType : inst.callArgTypes) {
              argTypes.push_back(ToLIRValueType(argType));
            }
            if (inst.hasResult) {
              lirFn.append<ir::CallInst>(inst.lirResultType, inst.dest,
                                         inst.symbol, std::move(args),
                                         std::move(argTypes));
            } else {
              lirFn.append<ir::CallInst>(inst.symbol, std::move(args),
                                         std::move(argTypes));
            }
            break;
          }
          case InstKind::Branch:
            lirFn.append<ir::BranchInst>(toLIROperand(inst.operands[0]), inst.trueTarget,
                                         inst.falseTarget);
            break;
          case InstKind::Jump:
            lirFn.append<ir::JumpInst>(inst.jumpTarget);
            break;
          case InstKind::Return:
            if (inst.hasValue && !inst.operands.empty()) {
              lirFn.append<ir::ReturnInst>(inst.lirValueType,
                                           toLIROperand(inst.operands[0]));
            } else {
              lirFn.append<ir::ReturnInst>();
            }
            break;
          case InstKind::Cmp:
          case InstKind::Cast:
            if (!inst.operands.empty()) {
              lirFn.append<ir::CopyInst>(inst.lirValueType, inst.dest,
                                         toLIROperand(inst.operands[0]));
            }
            break;
        }
      }
    }
    program.functions.push_back(std::move(lirFn));
  }

  return program;
}

void RebuildUseLists(Function& function) {
  if (function.nextValueId < 0) function.nextValueId = 0;
  if (static_cast<int>(function.values.size()) < function.nextValueId) {
    function.values.resize(function.nextValueId);
  }
  for (auto& info : function.values) {
    info.users.clear();
  }
  auto addUse = [&](const Operand& operand, int user) {
    if (!operand.isRegister()) return;
    if (operand.reg < 0) return;
    if (operand.reg >= static_cast<int>(function.values.size())) {
      function.values.resize(operand.reg + 1);
    }
    function.values[operand.reg].users.push_back(user);
  };

  for (auto& block : function.blocks) {
    for (auto& inst : block.instructions) {
      if (inst.hasResult && inst.dest >= 0) {
        if (inst.dest >= static_cast<int>(function.values.size())) {
          function.values.resize(inst.dest + 1);
        }
        function.values[inst.dest].type = inst.type;
      }
      for (const auto& operand : inst.operands) {
        addUse(operand, inst.dest);
      }
      for (const auto& incoming : inst.incomings) {
        addUse(incoming.second, inst.dest);
      }
    }
  }
}

bool VerifyMidFunction(const Function& function, std::string* error) {
  if (function.blocks.empty()) {
    if (error) *error = "function has no blocks";
    return false;
  }
  if (function.entryBlock < 0 || function.entryBlock >= static_cast<int>(function.blocks.size())) {
    if (error) *error = "invalid entry block";
    return false;
  }
  std::unordered_set<std::string> names;
  for (size_t bi = 0; bi < function.blocks.size(); ++bi) {
    const auto& block = function.blocks[bi];
    if (block.name.empty()) {
      if (error) *error = "empty block name";
      return false;
    }
    if (!names.insert(block.name).second) {
      if (error) *error = "duplicate block name: " + block.name;
      return false;
    }
    bool seenNonPhi = false;
    for (const auto& inst : block.instructions) {
      if (inst.kind == InstKind::Phi) {
        if (seenNonPhi) {
          if (error) *error = "phi not at block top";
          return false;
        }
      } else {
        seenNonPhi = true;
      }
      if (inst.hasResult && inst.dest < 0) {
        if (error) *error = "instruction marked hasResult without dest";
        return false;
      }
    }
    if (!block.instructions.empty()) {
      InstKind k = block.instructions.back().kind;
      if (k != InstKind::Branch && k != InstKind::Jump && k != InstKind::Return) {
        if (error) *error = "block missing terminator: " + block.name;
        return false;
      }
    }
  }
  return true;
}

bool VerifyMidIR(const Module& module, std::string* error) {
  for (const auto& function : module.functions) {
    if (!VerifyMidFunction(function, error)) return false;
  }
  return true;
}

DominatorTree BuildDominatorTree(const Function& function) {
  DominatorTree tree;
  const int n = static_cast<int>(function.blocks.size());
  tree.idom.assign(n, -1);
  tree.children.assign(n, {});
  tree.frontier.assign(n, {});
  if (n == 0 || function.entryBlock < 0) return tree;

  std::vector<int> rpo;
  rpo.reserve(n);
  std::vector<char> visited(n, 0);
  std::function<void(int)> dfs = [&](int blockIndex) {
    visited[blockIndex] = 1;
    for (int succ : function.blocks[blockIndex].succs) {
      if (succ < 0 || succ >= n || visited[succ]) continue;
      dfs(succ);
    }
    rpo.push_back(blockIndex);
  };
  dfs(function.entryBlock);
  std::reverse(rpo.begin(), rpo.end());

  std::vector<int> order(n, -1);
  for (size_t i = 0; i < rpo.size(); ++i) {
    order[rpo[i]] = static_cast<int>(i);
  }

  auto intersect = [&](int b1, int b2) {
    while (b1 != b2) {
      while (order[b1] > order[b2]) b1 = tree.idom[b1];
      while (order[b2] > order[b1]) b2 = tree.idom[b2];
    }
    return b1;
  };

  tree.idom[function.entryBlock] = function.entryBlock;
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 1; i < rpo.size(); ++i) {
      int b = rpo[i];
      const auto& preds = function.blocks[b].preds;
      int newIdom = -1;
      for (int p : preds) {
        if (p < 0 || p >= n || tree.idom[p] == -1) continue;
        newIdom = (newIdom == -1) ? p : intersect(p, newIdom);
      }
      if (newIdom != -1 && tree.idom[b] != newIdom) {
        tree.idom[b] = newIdom;
        changed = true;
      }
    }
  }

  for (int b = 0; b < n; ++b) {
    int id = tree.idom[b];
    if (b != function.entryBlock && id >= 0 && id < n) {
      tree.children[id].push_back(b);
    }
  }

  for (int b = 0; b < n; ++b) {
    if (function.blocks[b].preds.size() < 2) continue;
    for (int p : function.blocks[b].preds) {
      if (p < 0 || p >= n) continue;
      int runner = p;
      while (runner != -1 && runner != tree.idom[b]) {
        auto& df = tree.frontier[runner];
        if (std::find(df.begin(), df.end(), b) == df.end()) {
          df.push_back(b);
        }
        int next = tree.idom[runner];
        if (next == runner) break;
        runner = next;
      }
    }
  }

  return tree;
}

LoopInfo BuildLoopInfo(const Function& function, const DominatorTree& domTree) {
  LoopInfo info;
  const int n = static_cast<int>(function.blocks.size());
  for (int from = 0; from < n; ++from) {
    for (int to : function.blocks[from].succs) {
      int cur = from;
      bool dominates = false;
      while (cur != -1) {
        if (cur == to) {
          dominates = true;
          break;
        }
        if (cur == domTree.idom[cur]) break;
        cur = domTree.idom[cur];
      }
      if (!dominates) continue;

      Loop loop;
      loop.header = to;
      loop.latch = from;
      std::set<int> members = {to, from};
      std::queue<int> work;
      work.push(from);
      while (!work.empty()) {
        int b = work.front();
        work.pop();
        for (int pred : function.blocks[b].preds) {
          if (members.insert(pred).second) {
            work.push(pred);
          }
        }
      }
      loop.blocks.assign(members.begin(), members.end());
      for (int block : loop.blocks) {
        for (int succ : function.blocks[block].succs) {
          if (members.find(succ) == members.end()) {
            loop.exitBlocks.push_back(succ);
          }
        }
      }
      std::sort(loop.exitBlocks.begin(), loop.exitBlocks.end());
      loop.exitBlocks.erase(std::unique(loop.exitBlocks.begin(), loop.exitBlocks.end()),
                            loop.exitBlocks.end());

      if (!function.blocks[to].preds.empty()) {
        for (int pred : function.blocks[to].preds) {
          if (members.find(pred) == members.end()) {
            loop.preheader = pred;
            break;
          }
        }
      }
      info.loops.push_back(std::move(loop));
    }
  }
  return info;
}

MemorySSA BuildMemorySSA(const Function& function, const DominatorTree& domTree) {
  (void)domTree;
  MemorySSA mssa;
  const int n = static_cast<int>(function.blocks.size());
  mssa.blockLiveIn.assign(n, -1);
  mssa.blockLiveOut.assign(n, -1);

  MemoryAccess liveOnEntry;
  liveOnEntry.kind = MemoryAccess::Kind::LiveOnEntry;
  liveOnEntry.id = 0;
  liveOnEntry.block = function.entryBlock;
  mssa.accesses.push_back(liveOnEntry);

  int nextId = 1;
  for (int b = 0; b < n; ++b) {
    int incoming = mssa.accesses.front().id;
    if (function.blocks[b].preds.size() > 1) {
      MemoryAccess phi;
      phi.kind = MemoryAccess::Kind::Phi;
      phi.id = nextId++;
      phi.block = b;
      phi.inputs.resize(function.blocks[b].preds.size(), 0);
      incoming = phi.id;
      mssa.accesses.push_back(phi);
    }
    mssa.blockLiveIn[b] = incoming;
    int current = incoming;
    for (size_t ii = 0; ii < function.blocks[b].instructions.size(); ++ii) {
      const auto& inst = function.blocks[b].instructions[ii];
      if (inst.kind == InstKind::Store ||
          (inst.kind == InstKind::Call && !inst.isPureCall)) {
        MemoryAccess def;
        def.kind = MemoryAccess::Kind::Def;
        def.id = nextId++;
        def.block = b;
        def.instIndex = static_cast<int>(ii);
        def.incoming = current;
        current = def.id;
        mssa.instToAccess[encodeInstKey(b, static_cast<int>(ii))] = def.id;
        mssa.accesses.push_back(def);
      } else if (inst.kind == InstKind::Load ||
                 (inst.kind == InstKind::Call && inst.isPureCall)) {
        MemoryAccess use;
        use.kind = MemoryAccess::Kind::Use;
        use.id = nextId++;
        use.block = b;
        use.instIndex = static_cast<int>(ii);
        use.incoming = current;
        mssa.instToAccess[encodeInstKey(b, static_cast<int>(ii))] = use.id;
        mssa.accesses.push_back(use);
      }
    }
    mssa.blockLiveOut[b] = current;
  }
  return mssa;
}

void ConvertToSSA(Function& function) {
  rebuildEdges(function);
  rebuildFrameObjects(function);
  DominatorTree dom = BuildDominatorTree(function);

  struct VariableKey {
    enum class Kind { Register, FrameSlot };
    Kind kind = Kind::Register;
    int id = -1;

    bool operator==(const VariableKey& other) const {
      return kind == other.kind && id == other.id;
    }
  };

  struct VariableKeyHash {
    size_t operator()(const VariableKey& key) const {
      return (static_cast<size_t>(key.kind) << 28) ^ static_cast<size_t>(key.id + 1);
    }
  };

  struct PromoteVar {
    Type type = Type::Void();
    ir::ValueType lirType = ir::ValueType::I32;
    std::unordered_set<int> defBlocks;
    Operand undefValue = Operand::Invalid();
    int objectId = -1;
  };

  struct ValueChange {
    VariableKey key;
    Operand previousValue = Operand::Invalid();
    bool hadPrevious = false;
  };

  std::unordered_map<VariableKey, PromoteVar, VariableKeyHash> vars;
  std::unordered_set<int> renameableRegisters;

  auto trackVariable = [&](const VariableKey& key, Type type, ir::ValueType lirType,
                           int defBlock, Operand undefValue, int objectId) {
    auto& info = vars[key];
    info.type = type;
    info.lirType = lirType;
    info.undefValue = undefValue;
    info.objectId = objectId;
    if (defBlock >= 0) info.defBlocks.insert(defBlock);
    if (key.kind == VariableKey::Kind::Register) {
      renameableRegisters.insert(key.id);
    }
  };

  auto safeFrameSlot = [&](int offset) -> const VariableObject* {
    auto it = function.frameObjects.find(offset);
    if (it == function.frameObjects.end()) return nullptr;
    if (!it->second.renameable) {
      return nullptr;
    }
    return &it->second;
  };

  for (size_t bi = 0; bi < function.blocks.size(); ++bi) {
    const auto& block = function.blocks[bi];
    for (const auto& inst : block.instructions) {
      if (inst.kind == InstKind::Load && !inst.operands.empty() &&
          inst.operands[0].isFrame()) {
        if (const VariableObject* frame = safeFrameSlot(inst.operands[0].frameOffset)) {
          trackVariable({VariableKey::Kind::FrameSlot, inst.operands[0].frameOffset},
                        frame->type, frame->lirType, -1,
                        defaultValueForType(frame->type), frame->id);
        }
      } else if (inst.kind == InstKind::Store && inst.operands.size() >= 2 &&
                 inst.operands[1].isFrame()) {
        if (const VariableObject* frame = safeFrameSlot(inst.operands[1].frameOffset)) {
          trackVariable({VariableKey::Kind::FrameSlot, inst.operands[1].frameOffset},
                        frame->type, frame->lirType, static_cast<int>(bi),
                        defaultValueForType(frame->type), frame->id);
        }
      }

      if (inst.hasResult && inst.dest >= 0) {
        auto objectIt = function.registerObjects.find(inst.dest);
        if (objectIt != function.registerObjects.end() && objectIt->second.renameable) {
          Operand incoming = objectIt->second.kind == ObjectKind::ParamScalar
                                 ? Operand::Reg(inst.dest, objectIt->second.type)
                                 : Operand::Invalid();
          trackVariable({VariableKey::Kind::Register, inst.dest},
                        objectIt->second.type, objectIt->second.lirType,
                        static_cast<int>(bi), incoming, objectIt->second.id);
        }
      }
    }
  }

  std::unordered_map<int, std::vector<VariableKey>> blockPhis;
  std::unordered_map<int, std::unordered_set<int>> inserted;
  std::vector<VariableKey> allVars;
  allVars.reserve(vars.size());
  for (const auto& [key, _] : vars) allVars.push_back(key);

  for (size_t varIndex = 0; varIndex < allVars.size(); ++varIndex) {
    const auto& key = allVars[varIndex];
    const auto& info = vars[key];
    if (info.defBlocks.empty()) continue;
    std::queue<int> work;
    std::unordered_set<int> seen(info.defBlocks.begin(), info.defBlocks.end());
    for (int defBlock : info.defBlocks) work.push(defBlock);
    while (!work.empty()) {
      int cur = work.front();
      work.pop();
      for (int frontierBlock : dom.frontier[cur]) {
        if (inserted[frontierBlock].insert(static_cast<int>(varIndex)).second) {
          blockPhis[frontierBlock].push_back(key);
          if (seen.insert(frontierBlock).second) {
            work.push(frontierBlock);
          }
        }
      }
    }
  }

  for (auto& [blockIndex, phiVars] : blockPhis) {
    auto& block = function.blocks[blockIndex];
    std::vector<Instruction> newInsts;
    newInsts.reserve(block.instructions.size() + phiVars.size());
    for (const auto& key : phiVars) {
      const auto& info = vars[key];
      Instruction phi;
      phi.kind = InstKind::Phi;
      phi.type = info.type;
      phi.lirValueType = info.lirType;
      phi.hasResult = true;
      phi.dest = function.allocateValue(info.type);
      phi.frameOffset = (key.kind == VariableKey::Kind::FrameSlot) ? key.id : 0;
      phi.sourceReg = (key.kind == VariableKey::Kind::Register) ? key.id : -1;
      for (int pred : block.preds) {
        phi.incomings.push_back({pred, info.undefValue});
      }
      newInsts.push_back(std::move(phi));
    }
    newInsts.insert(newInsts.end(),
                    std::make_move_iterator(block.instructions.begin()),
                    std::make_move_iterator(block.instructions.end()));
    block.instructions = std::move(newInsts);
  }

  std::unordered_map<VariableKey, Operand, VariableKeyHash> currentValues;
  currentValues.reserve(vars.size());
  for (const auto& [key, info] : vars) {
    currentValues.emplace(key, info.undefValue);
  }
  std::vector<ValueChange> changeLog;
  changeLog.reserve(vars.size());

  auto setCurrentValue = [&](const VariableKey& key, Operand value) {
    auto it = currentValues.find(key);
    ValueChange change;
    change.key = key;
    if (it != currentValues.end()) {
      change.hadPrevious = true;
      change.previousValue = it->second;
      it->second = value;
    } else {
      change.hadPrevious = false;
      currentValues.emplace(key, value);
    }
    changeLog.push_back(std::move(change));
  };

  auto rollbackChanges = [&](size_t checkpoint) {
    while (changeLog.size() > checkpoint) {
      const ValueChange& change = changeLog.back();
      if (change.hadPrevious) {
        currentValues[change.key] = change.previousValue;
      } else {
        currentValues.erase(change.key);
      }
      changeLog.pop_back();
    }
  };

  std::function<void(int)> renameBlock = [&](int blockIndex) {
    size_t checkpoint = changeLog.size();
    auto& block = function.blocks[blockIndex];

    for (auto& inst : block.instructions) {
      if (inst.kind != InstKind::Phi) break;
      VariableKey key;
      if (inst.sourceReg >= 0) {
        key.kind = VariableKey::Kind::Register;
        key.id = inst.sourceReg;
      } else {
        key.kind = VariableKey::Kind::FrameSlot;
        key.id = inst.frameOffset;
      }
      setCurrentValue(key, Operand::Reg(inst.dest, inst.type));
    }

    std::vector<Instruction> rewritten;
    rewritten.reserve(block.instructions.size());
    for (auto& inst : block.instructions) {
      if (inst.kind == InstKind::Phi) {
        rewritten.push_back(std::move(inst));
        continue;
      }

      auto rewriteByCurrent = [&](Operand& operand) {
        if (!operand.isRegister()) return;
        if (renameableRegisters.find(operand.reg) == renameableRegisters.end()) return;
        VariableKey key{VariableKey::Kind::Register, operand.reg};
        auto it = currentValues.find(key);
        if (it != currentValues.end() && it->second.isValid()) {
          operand = it->second;
        }
      };

      for (auto& operand : inst.operands) rewriteByCurrent(operand);
      for (auto& incoming : inst.incomings) rewriteByCurrent(incoming.second);

      if (inst.kind == InstKind::Load && !inst.operands.empty() && inst.operands[0].isFrame()) {
        VariableKey key{VariableKey::Kind::FrameSlot, inst.operands[0].frameOffset};
        auto it = currentValues.find(key);
        if (it != currentValues.end()) {
          setCurrentValue({VariableKey::Kind::Register, inst.dest}, it->second);
          continue;
        }
      }

      if (inst.kind == InstKind::Store && inst.operands.size() >= 2 && inst.operands[1].isFrame()) {
        VariableKey key{VariableKey::Kind::FrameSlot, inst.operands[1].frameOffset};
        auto it = currentValues.find(key);
        if (it != currentValues.end()) {
          setCurrentValue(key, inst.operands[0]);
          continue;
        }
      }

      if (inst.hasResult && inst.dest >= 0) {
        auto objectIt = function.registerObjects.find(inst.dest);
        if (objectIt != function.registerObjects.end() && objectIt->second.renameable) {
          int originalDest = inst.dest;
          int freshDest = function.allocateValue(inst.type);
          inst.dest = freshDest;
          setCurrentValue({VariableKey::Kind::Register, originalDest},
                          Operand::Reg(freshDest, inst.type));
        }
      }
      rewritten.push_back(std::move(inst));
    }
    block.instructions = std::move(rewritten);

    for (int succ : block.succs) {
      auto& succBlock = function.blocks[succ];
      for (auto& phi : succBlock.instructions) {
        if (phi.kind != InstKind::Phi) break;
        VariableKey key;
        if (phi.sourceReg >= 0) {
          key.kind = VariableKey::Kind::Register;
          key.id = phi.sourceReg;
        } else {
          key.kind = VariableKey::Kind::FrameSlot;
          key.id = phi.frameOffset;
        }
        Operand incomingValue = vars[key].undefValue;
        auto it = currentValues.find(key);
        if (it != currentValues.end()) incomingValue = it->second;
        for (auto& incoming : phi.incomings) {
          if (incoming.first == blockIndex) {
            incoming.second = incomingValue;
            break;
          }
        }
      }
    }

    for (int child : dom.children[blockIndex]) {
      renameBlock(child);
    }

    rollbackChanges(checkpoint);
  };

  if (function.entryBlock >= 0) renameBlock(function.entryBlock);

  function.inSSA = true;
  rebuildFrameObjects(function);
  RebuildUseLists(function);
}

void OptimizeMidProgram(Module& module) {
  for (auto& function : module.functions) {
    CanonicalizeCFG(function);
    ConvertToSSA(function);
    CanonicalizeCFG(function);
    RebuildUseLists(function);
  }
}

}  // namespace midir

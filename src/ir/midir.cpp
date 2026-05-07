#include "midir.h"

#include <algorithm>
#include <functional>
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

}  // namespace

int Function::allocateValue(Type type) {
  int id = nextValueId++;
  if (static_cast<int>(values.size()) <= id) {
    values.resize(id + 1);
  }
  values[id].type = type;
  return id;
}

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
        fn.values[param].type = ToMidType(lirFn.paramTypes[pi],
                                          pi < lirFn.paramIsArray.size() && lirFn.paramIsArray[pi]);
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

    for (size_t bi = 0; bi < fn.blocks.size(); ++bi) {
      auto& block = fn.blocks[bi];
      if (block.instructions.empty()) continue;
      InstKind tail = block.instructions.back().kind;
      if (tail == InstKind::Branch || tail == InstKind::Jump || tail == InstKind::Return) {
        continue;
      }
      if (bi + 1 < fn.blocks.size()) {
        Instruction jump;
        jump.kind = InstKind::Jump;
        jump.type = Type::Void();
        jump.jumpTarget = fn.blocks[bi + 1].name;
        block.instructions.push_back(std::move(jump));
      } else {
        Instruction ret;
        ret.kind = InstKind::Return;
        ret.type = Type::Void();
        ret.hasValue = false;
        block.instructions.push_back(std::move(ret));
      }
    }

    fn.entryBlock = fn.blocks.empty() ? -1 : 0;
    rebuildEdges(fn);
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
    ir::IRFunction lirFn(midFn.name);
    lirFn.returnType = ToLIRValueType(midFn.returnType);
    lirFn.localArraySize = midFn.localArraySize;
    lirFn.nextVReg = midFn.nextValueId;
    lirFn.params = midFn.params;
    lirFn.paramIsArray = midFn.paramIsArray;
    for (const auto& type : midFn.paramTypes) {
      lirFn.paramTypes.push_back(ToLIRValueType(type));
    }

    for (const auto& block : midFn.blocks) {
      lirFn.append<ir::LabelInst>(block.name);
      for (const auto& inst : block.instructions) {
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
          case InstKind::Phi: {
            if (!inst.incomings.empty()) {
              lirFn.append<ir::CopyInst>(inst.lirValueType, inst.dest,
                                         toLIROperand(inst.incomings.front().second));
            }
            break;
          }
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

  std::vector<std::vector<int>> doms(n);
  for (int i = 0; i < n; ++i) {
    if (i == function.entryBlock) {
      doms[i] = {i};
    } else {
      doms[i].resize(n);
      for (int j = 0; j < n; ++j) doms[i][j] = j;
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (int b = 0; b < n; ++b) {
      if (b == function.entryBlock) continue;
      const auto& preds = function.blocks[b].preds;
      if (preds.empty()) continue;

      std::set<int> inter(doms[preds.front()].begin(), doms[preds.front()].end());
      for (size_t pi = 1; pi < preds.size(); ++pi) {
        std::set<int> next;
        for (int v : doms[preds[pi]]) {
          if (inter.find(v) != inter.end()) next.insert(v);
        }
        inter = std::move(next);
      }
      inter.insert(b);
      std::vector<int> newDom(inter.begin(), inter.end());
      if (newDom != doms[b]) {
        doms[b] = std::move(newDom);
        changed = true;
      }
    }
  }

  tree.idom[function.entryBlock] = function.entryBlock;
  for (int b = 0; b < n; ++b) {
    if (b == function.entryBlock) continue;
    int best = -1;
    for (int cand : doms[b]) {
      if (cand == b) continue;
      bool dominatedByAllOtherStrictDoms = true;
      for (int other : doms[b]) {
        if (other == b || other == cand) continue;
        if (std::find(doms[cand].begin(), doms[cand].end(), other) == doms[cand].end()) {
          dominatedByAllOtherStrictDoms = false;
          break;
        }
      }
      if (dominatedByAllOtherStrictDoms) {
        best = cand;
        break;
      }
    }
    tree.idom[b] = best;
  }

  for (int b = 0; b < n; ++b) {
    if (b == function.entryBlock) continue;
    int id = tree.idom[b];
    if (id >= 0 && id < n) tree.children[id].push_back(b);
  }

  for (int b = 0; b < n; ++b) {
    if (function.blocks[b].preds.size() < 2) continue;
    for (int p : function.blocks[b].preds) {
      int runner = p;
      while (runner != -1 && runner != tree.idom[b]) {
        auto& df = tree.frontier[runner];
        if (std::find(df.begin(), df.end(), b) == df.end()) df.push_back(b);
        if (runner == tree.idom[runner]) break;
        runner = tree.idom[runner];
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
  function.inSSA = true;
  RebuildUseLists(function);
}

void OptimizeMidProgram(Module& module) {
  for (auto& function : module.functions) {
    rebuildEdges(function);
    ConvertToSSA(function);
  }
}

}  // namespace midir

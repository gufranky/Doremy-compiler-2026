#include "midir_builder.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <unordered_set>
#include <utility>

#include "analysis.h"
#include "cfg.h"

namespace midir {

namespace {

using AstType = ::Type;

ValueRef operandToValue(const ir::Operand& operand,
                        const std::vector<Type>& legacyValueTypes) {
  if (operand.isImm()) {
    if (operand.valueType == ir::ValueType::F32) {
      return ValueRef::ImmediateFloat(operand.immFloatValue);
    }
    return ValueRef::ImmediateInt(operand.immValue);
  }
  if (operand.isGlobal()) {
    return ValueRef::Global(operand.globalName, toMidIRType(operand.valueType));
  }
  if (operand.isLocalVarAddr()) {
    return ValueRef::Frame(operand.immValue);
  }
  if (operand.isStackPtr()) {
    return ValueRef::StackPtr();
  }
  if (operand.isVReg()) {
    Type type = toMidIRType(operand.valueType);
    if (operand.vregId >= 0 &&
        operand.vregId < static_cast<int>(legacyValueTypes.size()) &&
        legacyValueTypes[operand.vregId] != Type::Void()) {
      type = legacyValueTypes[operand.vregId];
    }
    return ValueRef::SSA(operand.vregId, type);
  }
  return ValueRef::Invalid();
}

bool operandIsPointerLike(const ir::Operand& operand,
                          const std::vector<Type>& legacyValueTypes) {
  if (operand.isGlobal() || operand.isLocalVarAddr() || operand.isStackPtr()) {
    return true;
  }
  if (operand.isVReg() && operand.vregId >= 0 &&
      operand.vregId < static_cast<int>(legacyValueTypes.size())) {
    return legacyValueTypes[operand.vregId].kind == TypeKind::Ptr;
  }
  return false;
}

Type inferResultType(const ir::Instruction& instruction,
                     const std::vector<Type>& legacyValueTypes) {
  switch (instruction.kind) {
    case ir::InstKind::Binary: {
      const auto& inst = static_cast<const ir::BinaryInst&>(instruction);
      bool ptrLike = operandIsPointerLike(inst.lhs, legacyValueTypes) ||
                     operandIsPointerLike(inst.rhs, legacyValueTypes);
      return toMidIRType(inst.resultType, ptrLike);
    }
    case ir::InstKind::Unary: {
      const auto& inst = static_cast<const ir::UnaryInst&>(instruction);
      bool ptrLike = operandIsPointerLike(inst.operand, legacyValueTypes);
      return toMidIRType(inst.resultType, ptrLike);
    }
    case ir::InstKind::Copy: {
      const auto& inst = static_cast<const ir::CopyInst&>(instruction);
      bool ptrLike = operandIsPointerLike(inst.src, legacyValueTypes);
      return toMidIRType(inst.destType, ptrLike);
    }
    case ir::InstKind::Load: {
      const auto& inst = static_cast<const ir::LoadInst&>(instruction);
      return toMidIRType(inst.valueType);
    }
    case ir::InstKind::Call: {
      const auto& inst = static_cast<const ir::CallInst&>(instruction);
      return toMidIRType(inst.resultType);
    }
    default:
      return Type::Void();
  }
}

void inferLegacyTypes(const ir::IRFunction& bridgeFunction,
                      std::vector<Type>& legacyValueTypes) {
  std::vector<std::vector<size_t>> users(legacyValueTypes.size());

  auto ensureTypeSlot = [&](int id) {
    if (id >= 0 && id >= static_cast<int>(legacyValueTypes.size())) {
      legacyValueTypes.resize(id + 1, Type::Void());
      users.resize(id + 1);
    }
  };

  auto addUser = [&](const ir::Operand& operand, size_t instructionIndex) {
    if (!operand.isVReg()) return;
    ensureTypeSlot(operand.vregId);
    users[operand.vregId].push_back(instructionIndex);
  };

  auto getDest = [](const ir::Instruction& instruction) {
    switch (instruction.kind) {
      case ir::InstKind::Binary:
        return static_cast<const ir::BinaryInst&>(instruction).dest;
      case ir::InstKind::Unary:
        return static_cast<const ir::UnaryInst&>(instruction).dest;
      case ir::InstKind::Copy:
        return static_cast<const ir::CopyInst&>(instruction).dest;
      case ir::InstKind::Load:
        return static_cast<const ir::LoadInst&>(instruction).dest;
      case ir::InstKind::Call: {
        const auto& callInst = static_cast<const ir::CallInst&>(instruction);
        return callInst.hasDest ? callInst.dest : -1;
      }
      default:
        return -1;
    }
  };

  for (size_t i = 0; i < bridgeFunction.instructions.size(); ++i) {
    const auto& instruction = *bridgeFunction.instructions[i];
    int dest = getDest(instruction);
    ensureTypeSlot(dest);
    switch (instruction.kind) {
      case ir::InstKind::Binary: {
        const auto& inst = static_cast<const ir::BinaryInst&>(instruction);
        addUser(inst.lhs, i);
        addUser(inst.rhs, i);
        break;
      }
      case ir::InstKind::Unary: {
        const auto& inst = static_cast<const ir::UnaryInst&>(instruction);
        addUser(inst.operand, i);
        break;
      }
      case ir::InstKind::Copy: {
        const auto& inst = static_cast<const ir::CopyInst&>(instruction);
        addUser(inst.src, i);
        break;
      }
      default:
        break;
    }
  }

  std::deque<size_t> worklist;
  std::vector<bool> queued(bridgeFunction.instructions.size(), false);
  auto enqueue = [&](size_t instructionIndex) {
    if (!queued[instructionIndex]) {
      queued[instructionIndex] = true;
      worklist.push_back(instructionIndex);
    }
  };

  for (size_t i = 0; i < bridgeFunction.instructions.size(); ++i) {
    enqueue(i);
  }

  while (!worklist.empty()) {
    size_t instructionIndex = worklist.front();
    worklist.pop_front();
    queued[instructionIndex] = false;

    const auto& instruction = *bridgeFunction.instructions[instructionIndex];
    Type type = inferResultType(instruction, legacyValueTypes);
    if (type == Type::Void()) continue;

    int dest = getDest(instruction);
    if (dest < 0) continue;
    ensureTypeSlot(dest);

    if (legacyValueTypes[dest] == type) continue;

    Type mergedType = legacyValueTypes[dest];
    if (mergedType == Type::Void()) {
      mergedType = type;
    } else if (mergedType == Type::I32() && type == Type::Ptr()) {
      mergedType = Type::Ptr();
    } else if (mergedType == Type::Ptr() && type == Type::I32()) {
      mergedType = Type::Ptr();
    } else {
      continue;
    }

    if (legacyValueTypes[dest] == mergedType) continue;
    legacyValueTypes[dest] = mergedType;
    for (size_t userIndex : users[dest]) {
      enqueue(userIndex);
    }
  }
}

Instruction convertInstruction(const ir::Instruction& bridgeInstruction,
                               const std::vector<Type>& legacyValueTypes) {
  Instruction inst;
  switch (bridgeInstruction.kind) {
    case ir::InstKind::Binary: {
      const auto& bridge = static_cast<const ir::BinaryInst&>(bridgeInstruction);
      inst.kind = InstKind::Binary;
      inst.binary_op = bridge.op;
      inst.operand_type = toMidIRType(bridge.operandType);
      inst.result_type = inferResultType(bridgeInstruction, legacyValueTypes);
      inst.result_id = -1;
      inst.legacy_result_id = bridge.dest;
      inst.has_result = true;
      inst.operands.push_back(operandToValue(bridge.lhs, legacyValueTypes));
      inst.operands.push_back(operandToValue(bridge.rhs, legacyValueTypes));
      break;
    }
    case ir::InstKind::Unary: {
      const auto& bridge = static_cast<const ir::UnaryInst&>(bridgeInstruction);
      inst.kind = InstKind::Unary;
      inst.unary_op = bridge.op;
      inst.operand_type = toMidIRType(bridge.operandType);
      inst.result_type = inferResultType(bridgeInstruction, legacyValueTypes);
      inst.legacy_result_id = bridge.dest;
      inst.has_result = true;
      inst.operands.push_back(operandToValue(bridge.operand, legacyValueTypes));
      break;
    }
    case ir::InstKind::Copy: {
      const auto& bridge = static_cast<const ir::CopyInst&>(bridgeInstruction);
      inst.kind = InstKind::Copy;
      inst.result_type = inferResultType(bridgeInstruction, legacyValueTypes);
      inst.legacy_result_id = bridge.dest;
      inst.has_result = true;
      inst.operands.push_back(operandToValue(bridge.src, legacyValueTypes));
      break;
    }
    case ir::InstKind::Load: {
      const auto& bridge = static_cast<const ir::LoadInst&>(bridgeInstruction);
      inst.kind = InstKind::Load;
      inst.result_type = toMidIRType(bridge.valueType);
      inst.legacy_result_id = bridge.dest;
      inst.has_result = true;
      inst.operands.push_back(operandToValue(bridge.addr, legacyValueTypes));
      break;
    }
    case ir::InstKind::Store: {
      const auto& bridge = static_cast<const ir::StoreInst&>(bridgeInstruction);
      inst.kind = InstKind::Store;
      inst.operands.push_back(operandToValue(bridge.src, legacyValueTypes));
      inst.operands.push_back(operandToValue(bridge.addr, legacyValueTypes));
      break;
    }
    case ir::InstKind::Branch: {
      const auto& bridge = static_cast<const ir::BranchInst&>(bridgeInstruction);
      inst.kind = InstKind::Branch;
      inst.operands.push_back(operandToValue(bridge.cond, legacyValueTypes));
      inst.true_target = bridge.trueLabel;
      inst.false_target = bridge.falseLabel;
      break;
    }
    case ir::InstKind::Jump: {
      const auto& bridge = static_cast<const ir::JumpInst&>(bridgeInstruction);
      inst.kind = InstKind::Jump;
      inst.jump_target = bridge.target;
      break;
    }
    case ir::InstKind::Call: {
      const auto& bridge = static_cast<const ir::CallInst&>(bridgeInstruction);
      inst.kind = InstKind::Call;
      inst.callee = bridge.callee;
      inst.result_type = toMidIRType(bridge.resultType);
      inst.has_result = bridge.hasDest;
      inst.legacy_result_id = bridge.hasDest ? bridge.dest : -1;
      for (const auto& arg : bridge.args) {
        inst.operands.push_back(operandToValue(arg, legacyValueTypes));
      }
      for (ir::ValueType argType : bridge.argTypes) {
        inst.call_arg_types.push_back(toMidIRType(argType));
      }
      break;
    }
    case ir::InstKind::Return: {
      const auto& bridge = static_cast<const ir::ReturnInst&>(bridgeInstruction);
      inst.kind = InstKind::Return;
      inst.has_value = bridge.hasValue;
      if (bridge.hasValue) {
        inst.operands.push_back(operandToValue(bridge.value, legacyValueTypes));
      }
      break;
    }
    case ir::InstKind::Label:
      break;
  }
  return inst;
}

void rebuildEdges(Function& function) {
  function.block_index_by_name.clear();
  for (size_t i = 0; i < function.blocks.size(); ++i) {
    function.block_index_by_name[function.blocks[i].name] = static_cast<int>(i);
    function.blocks[i].preds.clear();
    function.blocks[i].succs.clear();
  }

  auto addEdge = [&](int from, int to) {
    if (from < 0 || to < 0) return;
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

  std::vector<BasicBlock> kept;
  kept.reserve(function.blocks.size());
  for (size_t i = 0; i < function.blocks.size(); ++i) {
    if (reachable[i]) kept.push_back(std::move(function.blocks[i]));
  }
  function.blocks = std::move(kept);
  function.entry_block = function.blocks.empty() ? -1 : 0;
  rebuildEdges(function);
}

void insertPhiNodes(Function& function, const DominatorTree& domTree) {
  std::vector<std::unordered_set<int>> definingBlocks(function.legacy_vreg_count);
  for (size_t i = 0; i < function.param_source_ids.size(); ++i) {
    int legacyId = function.param_source_ids[i];
    if (legacyId >= 0 && legacyId < function.legacy_vreg_count &&
        function.entry_block >= 0) {
      definingBlocks[legacyId].insert(function.entry_block);
    }
  }
  for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex) {
    for (const auto& inst : function.blocks[blockIndex].instructions) {
      if (inst.has_result && inst.legacy_result_id >= 0 &&
          inst.legacy_result_id < function.legacy_vreg_count) {
        definingBlocks[inst.legacy_result_id].insert(static_cast<int>(blockIndex));
      }
    }
  }

  std::vector<std::unordered_set<int>> phiPlaced(function.blocks.size());
  for (int variable = 0; variable < function.legacy_vreg_count; ++variable) {
    if (definingBlocks[variable].size() <= 1) continue;
    std::vector<int> work(definingBlocks[variable].begin(),
                          definingBlocks[variable].end());
    for (size_t workIndex = 0; workIndex < work.size(); ++workIndex) {
      int blockIndex = work[workIndex];
      for (int frontierBlock : domTree.frontier[blockIndex]) {
        if (phiPlaced[frontierBlock].insert(variable).second) {
          Instruction phi;
          phi.kind = InstKind::Phi;
          phi.result_type = variable < static_cast<int>(function.legacy_value_types.size())
                                ? function.legacy_value_types[variable]
                                : Type::I32();
          if (phi.result_type == Type::Void()) {
            phi.result_type = Type::I32();
          }
          phi.has_result = true;
          phi.legacy_result_id = variable;
          function.blocks[frontierBlock].instructions.insert(
              function.blocks[frontierBlock].instructions.begin(), std::move(phi));
          if (definingBlocks[variable].insert(frontierBlock).second) {
            work.push_back(frontierBlock);
          }
        }
      }
    }
  }
}

void renameToSSA(Function& function, const DominatorTree& domTree) {
  std::vector<std::vector<int>> stacks(function.legacy_vreg_count);
  function.params.clear();
  function.params.resize(function.param_source_ids.size(), -1);

  auto lookupCurrentValue = [&](int legacyId, Type fallbackType) {
    if (legacyId < 0 || legacyId >= function.legacy_vreg_count) {
      return ValueRef::Undef(fallbackType == Type::Void() ? Type::I32() : fallbackType);
    }
    if (stacks[legacyId].empty()) {
      Type type = fallbackType;
      if (type == Type::Void() &&
          legacyId < static_cast<int>(function.legacy_value_types.size()) &&
          function.legacy_value_types[legacyId] != Type::Void()) {
        type = function.legacy_value_types[legacyId];
      }
      if (type == Type::Void()) type = Type::I32();
      return ValueRef::Undef(type);
    }
    int ssaId = stacks[legacyId].back();
    Type type = ssaId < static_cast<int>(function.value_types.size())
                    ? function.value_types[ssaId]
                    : fallbackType;
    if (type == Type::Void()) {
      type = fallbackType == Type::Void() ? Type::I32() : fallbackType;
    }
    return ValueRef::SSA(ssaId, type);
  };

  std::function<void(int)> visit = [&](int blockIndex) {
    auto& block = function.blocks[blockIndex];
    std::vector<int> pushedVariables;

    if (blockIndex == function.entry_block) {
      for (size_t i = 0; i < function.param_source_ids.size(); ++i) {
        int legacyId = function.param_source_ids[i];
        if (legacyId < 0 || legacyId >= function.legacy_vreg_count) continue;
        Type type = i < function.param_types.size() ? function.param_types[i] : Type::I32();
        int ssaId = function.newValue(type);
        stacks[legacyId].push_back(ssaId);
        function.params[i] = ssaId;
        pushedVariables.push_back(legacyId);
      }
    }

    size_t index = 0;
    while (index < block.instructions.size() &&
           block.instructions[index].kind == InstKind::Phi) {
      auto& phi = block.instructions[index];
      int legacyId = phi.legacy_result_id;
      Type type = phi.result_type == Type::Void() ? Type::I32() : phi.result_type;
      int ssaId = function.newValue(type);
      phi.result_id = ssaId;
      stacks[legacyId].push_back(ssaId);
      pushedVariables.push_back(legacyId);
      ++index;
    }

    for (; index < block.instructions.size(); ++index) {
      auto& inst = block.instructions[index];
      for (auto& operand : inst.operands) {
        if (!operand.isSSA()) continue;
        int legacyId = operand.value_id;
        operand = lookupCurrentValue(legacyId, operand.type);
      }
      if (!inst.has_result) continue;
      int legacyId = inst.legacy_result_id;
      if (legacyId < 0 || legacyId >= function.legacy_vreg_count) continue;
      Type type = inst.result_type == Type::Void() ? Type::I32() : inst.result_type;
      int ssaId = function.newValue(type);
      inst.result_id = ssaId;
      stacks[legacyId].push_back(ssaId);
      pushedVariables.push_back(legacyId);
    }

    for (int succIndex : block.succs) {
      auto& succ = function.blocks[succIndex];
      for (auto& inst : succ.instructions) {
        if (inst.kind != InstKind::Phi) break;
        int legacyId = inst.legacy_result_id;
        inst.incomings.push_back(
            PhiIncoming{blockIndex, lookupCurrentValue(legacyId, inst.result_type)});
      }
    }

    for (int child : domTree.children[blockIndex]) {
      visit(child);
    }

    while (!pushedVariables.empty()) {
      int legacyId = pushedVariables.back();
      pushedVariables.pop_back();
      if (!stacks[legacyId].empty()) {
        stacks[legacyId].pop_back();
      }
    }
  };

  if (function.entry_block >= 0) {
    visit(function.entry_block);
  }
  function.in_ssa = true;
}

void finalizeFunctionSSA(Function& function) {
  rebuildEdges(function);
  pruneUnreachableBlocks(function);
  for (size_t i = 0; i < function.blocks.size(); ++i) {
    if (!function.blocks[i].hasTerminator()) {
      Instruction jump;
      if (i + 1 < function.blocks.size()) {
        jump.kind = InstKind::Jump;
        jump.jump_target = function.blocks[i + 1].name;
      } else {
        jump.kind = InstKind::Return;
        jump.has_value = false;
      }
      function.blocks[i].instructions.push_back(std::move(jump));
    }
  }
  rebuildEdges(function);
  DominatorTree domTree = buildDominatorTree(function);
  insertPhiNodes(function, domTree);
  renameToSSA(function, domTree);
}

Function buildFunctionFromBridge(const ir::IRFunction& bridgeFunction) {
  Function function;
  function.name = bridgeFunction.name;
  function.return_type = toMidIRType(bridgeFunction.returnType);
  function.local_array_size = bridgeFunction.localArraySize;
  function.legacy_vreg_count = bridgeFunction.nextVReg;
  function.legacy_value_types.assign(bridgeFunction.nextVReg, Type::Void());

  for (size_t i = 0; i < bridgeFunction.params.size(); ++i) {
    int legacyId = bridgeFunction.params[i];
    Type type = i < bridgeFunction.paramTypes.size()
                    ? toMidIRType(bridgeFunction.paramTypes[i],
                                  i < bridgeFunction.paramIsArray.size() &&
                                      bridgeFunction.paramIsArray[i])
                    : Type::I32();
    if (legacyId >= static_cast<int>(function.legacy_value_types.size())) {
      function.legacy_value_types.resize(legacyId + 1, Type::Void());
    }
    function.legacy_value_types[legacyId] = type;
    function.param_source_ids.push_back(legacyId);
    function.param_types.push_back(type);
    function.param_is_array.push_back(i < bridgeFunction.paramIsArray.size() &&
                                      bridgeFunction.paramIsArray[i]);
  }

  inferLegacyTypes(bridgeFunction, function.legacy_value_types);

  ir::ControlFlowGraph cfg =
      ir::ControlFlowGraph::Build(const_cast<ir::IRFunction*>(&bridgeFunction));

  std::unordered_set<const ir::BasicBlock*> reachable;
  if (cfg.entry) {
    std::vector<const ir::BasicBlock*> stack{cfg.entry};
    while (!stack.empty()) {
      const ir::BasicBlock* block = stack.back();
      stack.pop_back();
      if (!reachable.insert(block).second) continue;
      for (const ir::BasicBlock* succ : block->succs) {
        if (succ) stack.push_back(succ);
      }
    }
  }

  function.entry_block = cfg.entry ? 0 : -1;
  for (const auto& cfgBlock : cfg.blocks) {
    if (!reachable.empty() && reachable.count(cfgBlock.get()) == 0) {
      continue;
    }
    BasicBlock block;
    block.name = cfgBlock->name;
    for (ir::Instruction* instruction : cfgBlock->instructions) {
      if (instruction->kind == ir::InstKind::Label) continue;
      block.instructions.push_back(
          convertInstruction(*instruction, function.legacy_value_types));
    }
    function.blocks.push_back(std::move(block));
  }

  finalizeFunctionSSA(function);
  return function;
}

class DirectMidIRBuilder {
 public:
  Module build(CompUnit* root) {
    module_ = Module();
    functions_.clear();
    scopes_.clear();
    declareBuiltinFunctions();
    collectFunctionSignatures(root);
    enterScope();

    for (const auto& item : root->items) {
      if (auto* decl = dynamic_cast<VarDeclStmt*>(item.get())) {
        emitDecl(decl, true);
      }
    }

    for (const auto& item : root->items) {
      auto* func = dynamic_cast<FuncDef*>(item.get());
      if (!func) continue;
      module_.functions.push_back(buildFunction(func));
    }

    exitScope();
    return module_;
  }

 private:
  struct Binding {
    AstType type = AstType::Invalid();
    bool isGlobal = false;
    bool isConst = false;
    bool isArray = false;
    bool isArrayParam = false;
    bool hasConstValue = false;
    ScalarValue constValue = ScalarValue::Int(0);
    int legacy_id = -1;
    int stack_offset = 0;
    std::string global_name;
  };

  struct ExprResult {
    AstType type = AstType::Invalid();
    ValueRef value = ValueRef::Invalid();
    bool isArrayAccess = false;
    ValueRef addr = ValueRef::Invalid();
  };

  struct FunctionSignature {
    AstType returnType = AstType::Invalid();
    std::vector<AstType> paramTypes;
  };

  struct InitElement {
    bool isConst = true;
    ScalarValue constValue = ScalarValue::Int(0);
    Expr* expr = nullptr;
  };

  Module module_;
  Function* current_function_ = nullptr;
  int current_block_index_ = -1;
  int block_counter_ = 0;
  std::vector<std::unordered_map<std::string, Binding>> scopes_;
  std::vector<std::pair<std::string, std::string>> loop_stack_;
  std::unordered_map<std::string, FunctionSignature> functions_;

  static Type scalarMidType(const AstType& type) {
    if (type.base == BaseType::FLOAT) return Type::F32();
    if (type.base == BaseType::VOID) return Type::Void();
    return Type::I32();
  }

  static Type paramMidType(const AstType& type) {
    return type.isArray ? Type::Ptr() : scalarMidType(type);
  }

  static bool isIntType(const AstType& type) {
    return type.base == BaseType::INT && !type.isArray;
  }

  static bool isFloatType(const AstType& type) {
    return type.base == BaseType::FLOAT && !type.isArray;
  }

  static bool isNumericType(const AstType& type) {
    return isIntType(type) || isFloatType(type);
  }

  static bool isRelationalOp(BinaryExpr::OpType op) {
    switch (op) {
      case BinaryExpr::B_LT:
      case BinaryExpr::B_GT:
      case BinaryExpr::B_LE:
      case BinaryExpr::B_GE:
      case BinaryExpr::B_EQ:
      case BinaryExpr::B_NE:
        return true;
      default:
        return false;
    }
  }

  static ir::BinaryOp toBinaryOp(BinaryExpr::OpType op) {
    switch (op) {
      case BinaryExpr::B_ADD:
        return ir::BinaryOp::Add;
      case BinaryExpr::B_SUB:
        return ir::BinaryOp::Sub;
      case BinaryExpr::B_MUL:
        return ir::BinaryOp::Mul;
      case BinaryExpr::B_DIV:
        return ir::BinaryOp::Div;
      case BinaryExpr::B_MOD:
        return ir::BinaryOp::Mod;
      case BinaryExpr::B_LT:
        return ir::BinaryOp::Lt;
      case BinaryExpr::B_GT:
        return ir::BinaryOp::Gt;
      case BinaryExpr::B_LE:
        return ir::BinaryOp::Le;
      case BinaryExpr::B_GE:
        return ir::BinaryOp::Ge;
      case BinaryExpr::B_EQ:
        return ir::BinaryOp::Eq;
      case BinaryExpr::B_NE:
        return ir::BinaryOp::Ne;
      case BinaryExpr::B_AND:
      case BinaryExpr::B_OR:
        break;
    }
    return ir::BinaryOp::Add;
  }

  static AstType commonNumericType(const AstType& lhs, const AstType& rhs) {
    if (isFloatType(lhs) || isFloatType(rhs)) return AstType::Float();
    return AstType::Int();
  }

  void declareBuiltinFunctions() {
    functions_["getint"] = FunctionSignature{AstType::Int(), {}};
    functions_["getch"] = FunctionSignature{AstType::Int(), {}};
    functions_["getfloat"] = FunctionSignature{AstType::Float(), {}};

    functions_["putint"] = FunctionSignature{AstType::Void(), {AstType::Int()}};
    functions_["putch"] = FunctionSignature{AstType::Void(), {AstType::Int()}};
    functions_["putfloat"] = FunctionSignature{AstType::Void(), {AstType::Float()}};

    AstType intArrayParam = AstType::Int();
    intArrayParam.isArray = true;
    intArrayParam.firstDimUnsized = true;
    AstType floatArrayParam = AstType::Float();
    floatArrayParam.isArray = true;
    floatArrayParam.firstDimUnsized = true;

    functions_["getarray"] = FunctionSignature{AstType::Int(), {intArrayParam}};
    functions_["getfarray"] = FunctionSignature{AstType::Int(), {floatArrayParam}};
    functions_["putarray"] =
        FunctionSignature{AstType::Void(), {AstType::Int(), intArrayParam}};
    functions_["putfarray"] =
        FunctionSignature{AstType::Void(), {AstType::Int(), floatArrayParam}};
    functions_["_sysy_starttime"] =
        FunctionSignature{AstType::Void(), {AstType::Int()}};
    functions_["_sysy_stoptime"] =
        FunctionSignature{AstType::Void(), {AstType::Int()}};
  }

  void collectFunctionSignatures(CompUnit* root) {
    for (const auto& item : root->items) {
      auto* func = dynamic_cast<FuncDef*>(item.get());
      if (!func) continue;
      FunctionSignature sig;
      sig.returnType = func->returnType;
      for (const auto& param : func->params) {
        sig.paramTypes.push_back(param->type);
      }
      functions_[func->name] = sig;
    }
  }

  void enterScope() { scopes_.emplace_back(); }

  void exitScope() {
    if (!scopes_.empty()) scopes_.pop_back();
  }

  Binding* lookupBinding(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) return &found->second;
    }
    return nullptr;
  }

  Binding& declareLocalValue(const std::string& name, Binding binding) {
    auto result = scopes_.back().emplace(name, std::move(binding));
    return result.first->second;
  }

  Binding& declareGlobalValue(const std::string& name, Binding binding) {
    auto result = scopes_.front().emplace(name, std::move(binding));
    return result.first->second;
  }

  int newLegacyValue(Type type) {
    int id = current_function_->legacy_vreg_count++;
    if (static_cast<int>(current_function_->legacy_value_types.size()) <= id) {
      current_function_->legacy_value_types.resize(id + 1, Type::Void());
    }
    current_function_->legacy_value_types[id] = type;
    return id;
  }

  int newBlock(const std::string& prefix) {
    BasicBlock block;
    block.name = prefix + std::to_string(block_counter_++);
    current_function_->blocks.push_back(std::move(block));
    if (current_function_->entry_block < 0) current_function_->entry_block = 0;
    return static_cast<int>(current_function_->blocks.size()) - 1;
  }

  std::string blockName(int index) const {
    return current_function_->blocks[static_cast<size_t>(index)].name;
  }

  BasicBlock& currentBlock() {
    return current_function_->blocks[static_cast<size_t>(current_block_index_)];
  }

  bool currentBlockTerminated() const {
    if (!current_function_ || current_block_index_ < 0) return true;
    return current_function_->blocks[static_cast<size_t>(current_block_index_)]
        .hasTerminator();
  }

  void switchToBlock(int blockIndex) { current_block_index_ = blockIndex; }

  void appendInstruction(Instruction inst) {
    currentBlock().instructions.push_back(std::move(inst));
  }

  ValueRef emitCopyResult(Type resultType, const ValueRef& src) {
    int legacyId = newLegacyValue(resultType);
    Instruction inst;
    inst.kind = InstKind::Copy;
    inst.result_type = resultType;
    inst.legacy_result_id = legacyId;
    inst.has_result = true;
    inst.operands.push_back(src);
    appendInstruction(std::move(inst));
    return ValueRef::SSA(legacyId, resultType);
  }

  void assignLegacyValue(int legacyId, Type type, const ValueRef& src) {
    Instruction inst;
    inst.kind = InstKind::Copy;
    inst.result_type = type;
    inst.legacy_result_id = legacyId;
    inst.has_result = true;
    inst.operands.push_back(src);
    appendInstruction(std::move(inst));
    if (legacyId >= 0 &&
        legacyId < static_cast<int>(current_function_->legacy_value_types.size()) &&
        current_function_->legacy_value_types[legacyId] == Type::Void()) {
      current_function_->legacy_value_types[legacyId] = type;
    }
  }

  ValueRef emitUnaryResult(ir::UnaryOp op, Type operandType, Type resultType,
                           const ValueRef& operand) {
    int legacyId = newLegacyValue(resultType);
    Instruction inst;
    inst.kind = InstKind::Unary;
    inst.unary_op = op;
    inst.operand_type = operandType;
    inst.result_type = resultType;
    inst.legacy_result_id = legacyId;
    inst.has_result = true;
    inst.operands.push_back(operand);
    appendInstruction(std::move(inst));
    return ValueRef::SSA(legacyId, resultType);
  }

  ValueRef emitBinaryResult(ir::BinaryOp op, Type operandType, Type resultType,
                            const ValueRef& lhs, const ValueRef& rhs) {
    int legacyId = newLegacyValue(resultType);
    Instruction inst;
    inst.kind = InstKind::Binary;
    inst.binary_op = op;
    inst.operand_type = operandType;
    inst.result_type = resultType;
    inst.legacy_result_id = legacyId;
    inst.has_result = true;
    inst.operands.push_back(lhs);
    inst.operands.push_back(rhs);
    appendInstruction(std::move(inst));
    return ValueRef::SSA(legacyId, resultType);
  }

  ValueRef emitLoadResult(Type resultType, const ValueRef& addr) {
    int legacyId = newLegacyValue(resultType);
    Instruction inst;
    inst.kind = InstKind::Load;
    inst.result_type = resultType;
    inst.legacy_result_id = legacyId;
    inst.has_result = true;
    inst.operands.push_back(addr);
    appendInstruction(std::move(inst));
    return ValueRef::SSA(legacyId, resultType);
  }

  void emitStore(const ValueRef& value, const ValueRef& addr) {
    Instruction inst;
    inst.kind = InstKind::Store;
    inst.operands.push_back(value);
    inst.operands.push_back(addr);
    appendInstruction(std::move(inst));
  }

  ExprResult castExprResult(const ExprResult& result, const AstType& targetType) {
    ExprResult casted = result;
    casted.value = castValue(result.value, result.type, targetType);
    casted.type = targetType.withoutConst();
    return casted;
  }

  ValueRef castValue(const ValueRef& value, const AstType& from, const AstType& to) {
    if (!from.isValid() || !to.isValid() || from.equalsIgnoringConst(to)) {
      return value;
    }
    if (!isNumericType(from) || !isNumericType(to)) return value;

    if (value.kind == ValueRef::Kind::ImmediateFloat) {
      if (isFloatType(to)) return value;
      return ValueRef::ImmediateInt(static_cast<int>(value.float_value));
    }
    if (value.kind == ValueRef::Kind::ImmediateInt) {
      if (isFloatType(to)) {
        return ValueRef::ImmediateFloat(static_cast<float>(value.int_value));
      }
      return value;
    }
    return emitCopyResult(scalarMidType(to), value);
  }

  AstType arrayElementType(const AstType& type) const {
    AstType result;
    result.base = type.base;
    result.isConst = type.isConst;

    int totalDims = static_cast<int>(type.arrayDimensions.size()) +
                    (type.firstDimUnsized ? 1 : 0);
    int remaining = totalDims - 1;
    if (remaining > 0) {
      result.isArray = true;
      if (type.firstDimUnsized) {
        result.arrayDimensions = type.arrayDimensions;
      } else {
        result.arrayDimensions = std::vector<int>(type.arrayDimensions.begin() + 1,
                                                  type.arrayDimensions.end());
      }
      result.firstDimUnsized = false;
    } else {
      result.isArray = false;
    }
    return result;
  }

  ScalarValue evalConstExpr(Expr* expr) {
    if (!expr) return ScalarValue::Int(0);
    if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
      return num->scalarValue;
    }
    if (auto* paren = dynamic_cast<ParenExpr*>(expr)) {
      return evalConstExpr(paren->expr.get());
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
      ScalarValue operand = evalConstExpr(unary->operand.get());
      switch (unary->op) {
        case UnaryExpr::U_PLUS:
          return operand;
        case UnaryExpr::U_MINUS:
          if (operand.isFloat()) {
            return ScalarValue::Float(-operand.floatValue);
          }
          return ScalarValue::Int(-operand.intValue);
        case UnaryExpr::U_NOT:
          if (operand.isFloat()) {
            return ScalarValue::Int(operand.floatValue == 0.0f ? 1 : 0);
          }
          return ScalarValue::Int(operand.intValue == 0 ? 1 : 0);
      }
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
      ScalarValue lhs = evalConstExpr(binary->left.get());
      ScalarValue rhs = evalConstExpr(binary->right.get());
      bool useFloat = lhs.isFloat() || rhs.isFloat();
      float lf = lhs.isFloat() ? lhs.floatValue : static_cast<float>(lhs.intValue);
      float rf = rhs.isFloat() ? rhs.floatValue : static_cast<float>(rhs.intValue);
      int li = lhs.isFloat() ? static_cast<int>(lhs.floatValue) : lhs.intValue;
      int ri = rhs.isFloat() ? static_cast<int>(rhs.floatValue) : rhs.intValue;
      switch (binary->op) {
        case BinaryExpr::B_ADD:
          return useFloat ? ScalarValue::Float(lf + rf)
                          : ScalarValue::Int(li + ri);
        case BinaryExpr::B_SUB:
          return useFloat ? ScalarValue::Float(lf - rf)
                          : ScalarValue::Int(li - ri);
        case BinaryExpr::B_MUL:
          return useFloat ? ScalarValue::Float(lf * rf)
                          : ScalarValue::Int(li * ri);
        case BinaryExpr::B_DIV:
          return useFloat ? ScalarValue::Float(lf / rf)
                          : ScalarValue::Int(li / ri);
        case BinaryExpr::B_MOD:
          return ScalarValue::Int(li % ri);
        case BinaryExpr::B_LT:
          return ScalarValue::Int(useFloat ? (lf < rf) : (li < ri));
        case BinaryExpr::B_GT:
          return ScalarValue::Int(useFloat ? (lf > rf) : (li > ri));
        case BinaryExpr::B_LE:
          return ScalarValue::Int(useFloat ? (lf <= rf) : (li <= ri));
        case BinaryExpr::B_GE:
          return ScalarValue::Int(useFloat ? (lf >= rf) : (li >= ri));
        case BinaryExpr::B_EQ:
          return ScalarValue::Int(useFloat ? (lf == rf) : (li == ri));
        case BinaryExpr::B_NE:
          return ScalarValue::Int(useFloat ? (lf != rf) : (li != ri));
        case BinaryExpr::B_AND:
          return ScalarValue::Int((useFloat ? lf != 0.0f : li != 0) &&
                                  (rhs.isFloat() ? rhs.floatValue != 0.0f
                                                 : rhs.intValue != 0));
        case BinaryExpr::B_OR:
          return ScalarValue::Int((useFloat ? lf != 0.0f : li != 0) ||
                                  (rhs.isFloat() ? rhs.floatValue != 0.0f
                                                 : rhs.intValue != 0));
      }
    }
    return ScalarValue::Int(0);
  }

  int calcArraySize(const std::vector<int>& dimensions) const {
    int size = 1;
    for (int dim : dimensions) size *= dim;
    return size;
  }

  bool isZeroInitializedArray(VarDef* def) const {
    if (!def || !def->hasInit) return true;
    if (!def->hasInitList || !def->initList) return false;
    std::function<bool(const InitList*)> isZeroList = [&](const InitList* list) {
      if (!list) return true;
      if (list->elements.empty()) return true;
      if (list->isScalar) {
        if (list->elements.empty() || !list->elements[0]) return true;
        auto* expr = dynamic_cast<Expr*>(list->elements[0].get());
        auto* num = expr ? dynamic_cast<NumberExpr*>(expr) : nullptr;
        if (!num) return false;
        return num->scalarValue.isFloat() ? num->scalarValue.floatValue == 0.0f
                                          : num->scalarValue.intValue == 0;
      }
      for (const auto& elem : list->elements) {
        if (!elem) continue;
        if (auto* sub = dynamic_cast<InitList*>(elem.get())) {
          if (!isZeroList(sub)) return false;
          continue;
        }
        auto* expr = dynamic_cast<Expr*>(elem.get());
        auto* num = expr ? dynamic_cast<NumberExpr*>(expr) : nullptr;
        if (!num) return false;
        bool isZero = num->scalarValue.isFloat() ? num->scalarValue.floatValue == 0.0f
                                                 : num->scalarValue.intValue == 0;
        if (!isZero) return false;
      }
      return true;
    };
    return isZeroList(def->initList.get());
  }

  void collectInitElements(InitList* initList, std::vector<InitElement>& result,
                           const std::vector<int>& dimensions) {
    if (!initList || dimensions.empty()) return;

    int totalSize = calcArraySize(dimensions);
    if (initList->isScalar) {
      InitElement elem;
      if (!initList->elements.empty() && initList->elements[0]) {
        if (auto* expr = dynamic_cast<Expr*>(initList->elements[0].get())) {
          if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
            elem.isConst = true;
            elem.constValue = num->scalarValue;
          } else {
            elem.isConst = false;
            elem.expr = expr;
          }
        }
      }
      result.push_back(elem);
      return;
    }

    int subArraySize = 1;
    if (dimensions.size() > 1) {
      for (size_t i = 1; i < dimensions.size(); ++i) {
        subArraySize *= dimensions[i];
      }
    }

    size_t startSize = result.size();
    size_t maxEndSize = startSize + static_cast<size_t>(totalSize);

    for (size_t elemIdx = 0; elemIdx < initList->elements.size(); ++elemIdx) {
      auto& elem = initList->elements[elemIdx];
      if (result.size() >= maxEndSize) break;

      if (!elem) {
        result.push_back(InitElement{true, ScalarValue::Int(0), nullptr});
      } else if (auto* subList = dynamic_cast<InitList*>(elem.get())) {
        if (subList->isScalar) {
          InitElement initElem;
          if (!subList->elements.empty() && subList->elements[0]) {
            if (auto* expr = dynamic_cast<Expr*>(subList->elements[0].get())) {
              if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
                initElem.isConst = true;
                initElem.constValue = num->scalarValue;
              } else {
                initElem.isConst = false;
                initElem.expr = expr;
              }
            }
          }
          result.push_back(initElem);
        } else {
          if (dimensions.size() > 1) {
            std::vector<int> subDims(dimensions.begin() + 1, dimensions.end());
            size_t prevSize = result.size();
            collectInitElements(subList, result, subDims);
            int filled = static_cast<int>(result.size() - prevSize);
            for (int j = filled; j < subArraySize && result.size() < maxEndSize; ++j) {
              result.push_back(InitElement{true, ScalarValue::Int(0), nullptr});
            }
          } else {
            collectInitElements(subList, result, {1});
          }
        }
      } else if (auto* expr = dynamic_cast<Expr*>(elem.get())) {
        InitElement initElem;
        if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
          initElem.isConst = true;
          initElem.constValue = num->scalarValue;
        } else {
          initElem.isConst = false;
          initElem.expr = expr;
        }
        result.push_back(initElem);
      }
    }
  }

  void flattenInitListWithDims(InitList* initList, std::vector<ScalarValue>& result,
                               const std::vector<int>& dimensions) {
    if (!initList || dimensions.empty()) return;

    int totalSize = calcArraySize(dimensions);
    if (initList->isScalar) {
      if (!initList->elements.empty() && initList->elements[0]) {
        if (auto* expr = dynamic_cast<Expr*>(initList->elements[0].get())) {
          result.push_back(evalConstExpr(expr));
        } else {
          result.push_back(ScalarValue::Int(0));
        }
      } else {
        result.push_back(ScalarValue::Int(0));
      }
      return;
    }

    int subArraySize = 1;
    if (dimensions.size() > 1) {
      for (size_t i = 1; i < dimensions.size(); ++i) {
        subArraySize *= dimensions[i];
      }
    }

    size_t startSize = result.size();
    size_t maxEndSize = startSize + static_cast<size_t>(totalSize);
    for (size_t elemIdx = 0; elemIdx < initList->elements.size(); ++elemIdx) {
      auto& elem = initList->elements[elemIdx];
      if (result.size() >= maxEndSize) break;

      if (!elem) {
        result.push_back(ScalarValue::Int(0));
      } else if (auto* subList = dynamic_cast<InitList*>(elem.get())) {
        if (subList->isScalar) {
          if (!subList->elements.empty() && subList->elements[0]) {
            if (auto* expr = dynamic_cast<Expr*>(subList->elements[0].get())) {
              result.push_back(evalConstExpr(expr));
            } else {
              result.push_back(ScalarValue::Int(0));
            }
          } else {
            result.push_back(ScalarValue::Int(0));
          }
        } else {
          if (dimensions.size() > 1) {
            std::vector<int> subDims(dimensions.begin() + 1, dimensions.end());
            size_t prevSize = result.size();
            flattenInitListWithDims(subList, result, subDims);
            int filled = static_cast<int>(result.size() - prevSize);
            for (int j = filled; j < subArraySize && result.size() < maxEndSize; ++j) {
              result.push_back(ScalarValue::Int(0));
            }
          } else {
            flattenInitListWithDims(subList, result, {1});
          }
        }
      } else if (auto* expr = dynamic_cast<Expr*>(elem.get())) {
        result.push_back(evalConstExpr(expr));
      }
    }
  }

  void emitArrayElementStore(const ValueRef& addr, const InitElement& element,
                             const AstType& elemType) {
    if (element.isConst) {
      if (elemType.base == BaseType::FLOAT) {
        emitStore(ValueRef::ImmediateFloat(element.constValue.isFloat()
                                               ? element.constValue.floatValue
                                               : static_cast<float>(element.constValue.intValue)),
                  addr);
      } else {
        emitStore(ValueRef::ImmediateInt(element.constValue.isFloat()
                                             ? static_cast<int>(element.constValue.floatValue)
                                             : element.constValue.intValue),
                  addr);
      }
      return;
    }

    ExprResult value = genExprResult(element.expr);
    value = castExprResult(value, elemType.withoutConst());
    emitStore(value.value, addr);
  }

  void zeroInitializeLocalArray(const Binding& binding, const AstType& elemType,
                                int totalElements) {
    for (int i = 0; i < totalElements; ++i) {
      ValueRef addr = ValueRef::Frame(binding.stack_offset + i * 4);
      if (elemType.base == BaseType::FLOAT) {
        emitStore(ValueRef::ImmediateFloat(0.0f), addr);
      } else {
        emitStore(ValueRef::ImmediateInt(0), addr);
      }
    }
  }

  void emitDecl(VarDeclStmt* decl, bool isGlobal) {
    for (auto& defPtr : decl->defs) {
      VarDef* def = defPtr.get();
      if (def->isArray) {
        int arraySize = 1;
        for (int dim : def->arrayDimensions) arraySize *= dim;
        bool zeroInitialized = isZeroInitializedArray(def);

        if (isGlobal) {
          Binding binding;
          binding.isGlobal = true;
          binding.isConst = decl->declaredType.isConst;
          binding.isArray = true;
          binding.type = decl->declaredType;
          binding.type.isArray = true;
          binding.type.arrayDimensions = def->arrayDimensions;
          binding.global_name = def->name;
          declareGlobalValue(def->name, binding);

          std::vector<ScalarValue> initValues;
          if (!zeroInitialized && def->hasInitList && def->initList) {
            flattenInitListWithDims(def->initList.get(), initValues,
                                    def->arrayDimensions);
            while (initValues.size() < static_cast<size_t>(arraySize)) {
              initValues.push_back(decl->declaredType.base == BaseType::FLOAT
                                       ? ScalarValue::Float(0.0f)
                                       : ScalarValue::Int(0));
            }
          }
          module_.global_arrays.push_back(GlobalArray{def->name,
                                                      def->arrayDimensions,
                                                      scalarMidType(decl->declaredType),
                                                      initValues,
                                                      decl->declaredType.isConst});
          continue;
        }

        Binding binding;
        binding.type = decl->declaredType;
        binding.type.isArray = true;
        binding.type.arrayDimensions = def->arrayDimensions;
        binding.type.firstDimUnsized = false;
        binding.isConst = decl->declaredType.isConst;
        binding.isArray = true;
        binding.stack_offset = current_function_->local_array_size;
        current_function_->local_array_size += arraySize * 4;
        auto& slot = declareLocalValue(def->name, binding);

        zeroInitializeLocalArray(slot, decl->declaredType, arraySize);
        if (!zeroInitialized && def->hasInitList && def->initList) {
          std::vector<InitElement> initElements;
          collectInitElements(def->initList.get(), initElements,
                              def->arrayDimensions);
          for (size_t i = 0;
               i < initElements.size() && i < static_cast<size_t>(arraySize); ++i) {
            ValueRef addr = ValueRef::Frame(slot.stack_offset +
                                            static_cast<int>(i) * 4);
            emitArrayElementStore(addr, initElements[i],
                                  decl->declaredType.withoutConst());
          }
        }
        continue;
      }

      if (isGlobal) {
        ScalarValue initValue =
            def->hasInit && def->initIsConst
                ? def->typedConstInitValue
                : (decl->declaredType.base == BaseType::FLOAT
                       ? ScalarValue::Float(0.0f)
                       : ScalarValue::Int(0));
        module_.globals.push_back(
            GlobalVar{def->name, initValue, scalarMidType(decl->declaredType),
                      decl->declaredType.isConst});

        Binding binding;
        binding.type = decl->declaredType;
        binding.isGlobal = true;
        binding.isConst = decl->declaredType.isConst;
        binding.hasConstValue = decl->declaredType.isConst && def->hasInit &&
                                def->initIsConst;
        binding.constValue = initValue;
        binding.global_name = def->name;
        declareGlobalValue(def->name, binding);
        continue;
      }

      Binding binding;
      binding.type = decl->declaredType;
      binding.isConst = decl->declaredType.isConst;
      binding.hasConstValue = decl->declaredType.isConst && def->hasInit &&
                              def->initIsConst;
      binding.constValue = binding.hasConstValue
                               ? def->typedConstInitValue
                               : ScalarValue::Int(0);
      binding.legacy_id = newLegacyValue(scalarMidType(decl->declaredType));
      auto& slot = declareLocalValue(def->name, binding);
      if (def->hasInit && def->initExpr) {
        ExprResult init =
            castExprResult(genExprResult(def->initExpr.get()),
                           decl->declaredType.withoutConst());
        assignLegacyValue(slot.legacy_id, scalarMidType(slot.type), init.value);
      } else if (!slot.isConst) {
        if (slot.type.base == BaseType::FLOAT) {
          assignLegacyValue(slot.legacy_id, scalarMidType(slot.type),
                            ValueRef::ImmediateFloat(0.0f));
        } else {
          assignLegacyValue(slot.legacy_id, scalarMidType(slot.type),
                            ValueRef::ImmediateInt(0));
        }
      }
    }
  }

  ExprResult genExprResult(Expr* expr) {
    if (auto* num = dynamic_cast<NumberExpr*>(expr)) {
      ExprResult result;
      result.type = num->isFloatLiteral() ? AstType::Float() : AstType::Int();
      result.value = num->isFloatLiteral()
                         ? ValueRef::ImmediateFloat(num->scalarValue.floatValue)
                         : ValueRef::ImmediateInt(num->scalarValue.intValue);
      return result;
    }

    if (auto* id = dynamic_cast<IdentifierExpr*>(expr)) {
      Binding* binding = lookupBinding(id->name);
      if (!binding) return ExprResult{};

      if (binding->isArray) {
        ExprResult result;
        result.type = binding->type.withoutConst();
        result.isArrayAccess = true;
        if (binding->isGlobal) {
          result.addr = ValueRef::Global(binding->global_name, Type::Ptr());
        } else if (binding->isArrayParam) {
          result.addr = ValueRef::SSA(binding->legacy_id, Type::Ptr());
        } else {
          result.addr = ValueRef::Frame(binding->stack_offset);
        }
        result.value = result.addr;
        return result;
      }

      if (binding->isConst && binding->hasConstValue && !binding->isGlobal) {
        ExprResult result;
        result.type = binding->type.withoutConst();
        result.value = binding->constValue.isFloat()
                           ? ValueRef::ImmediateFloat(binding->constValue.floatValue)
                           : ValueRef::ImmediateInt(binding->constValue.intValue);
        return result;
      }

      ExprResult result;
      result.type = binding->type.withoutConst();
      if (binding->isGlobal) {
        if (binding->isConst && binding->hasConstValue) {
          result.value = binding->constValue.isFloat()
                             ? ValueRef::ImmediateFloat(binding->constValue.floatValue)
                             : ValueRef::ImmediateInt(binding->constValue.intValue);
        } else {
          result.value = emitLoadResult(
              scalarMidType(binding->type),
              ValueRef::Global(binding->global_name, Type::Ptr()));
        }
      } else {
        result.value = ValueRef::SSA(binding->legacy_id, scalarMidType(binding->type));
      }
      return result;
    }

    if (auto* paren = dynamic_cast<ParenExpr*>(expr)) {
      return genExprResult(paren->expr.get());
    }

    if (auto* arrAccess = dynamic_cast<ArrayAccessExpr*>(expr)) {
      return genArrayAccess(arrAccess);
    }

    if (auto* call = dynamic_cast<FunctionCallExpr*>(expr)) {
      std::vector<ValueRef> args;
      std::vector<Type> argTypes;
      AstType resultType = AstType::Int();
      auto sigIt = functions_.find(call->funcName);
      if (sigIt != functions_.end()) resultType = sigIt->second.returnType;

      for (size_t i = 0; i < call->args.size(); ++i) {
        ExprResult arg = genExprResult(call->args[i].get());
        bool paramIsArray = false;
        AstType paramType = AstType::Invalid();
        if (sigIt != functions_.end() && i < sigIt->second.paramTypes.size()) {
          paramIsArray = sigIt->second.paramTypes[i].isArray;
          paramType = sigIt->second.paramTypes[i];
        }

        if (arg.isArrayAccess && paramIsArray) {
          args.push_back(arg.addr);
          argTypes.push_back(Type::Ptr());
        } else {
          if (paramType.isValid()) {
            arg = castExprResult(arg, paramType.withoutConst());
          }
          args.push_back(arg.value);
          argTypes.push_back(arg.value.type);
        }
      }

      Instruction inst;
      inst.kind = InstKind::Call;
      inst.callee = call->funcName;
      inst.call_arg_types = argTypes;
      inst.operands = args;
      inst.has_result = resultType.base != BaseType::VOID;
      if (inst.has_result) {
        inst.result_type = scalarMidType(resultType);
        inst.legacy_result_id = newLegacyValue(inst.result_type);
      }
      appendInstruction(std::move(inst));

      ExprResult result;
      result.type = resultType.withoutConst();
      if (resultType.base != BaseType::VOID) {
        result.value = ValueRef::SSA(currentBlock().instructions.back().legacy_result_id,
                                     scalarMidType(result.type));
      }
      return result;
    }

    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
      ExprResult operand = genExprResult(unary->operand.get());
      switch (unary->op) {
        case UnaryExpr::U_PLUS:
          return operand;
        case UnaryExpr::U_MINUS: {
          ExprResult result;
          result.type = operand.type.withoutConst();
          result.value = emitUnaryResult(ir::UnaryOp::Neg,
                                         scalarMidType(operand.type),
                                         scalarMidType(operand.type),
                                         operand.value);
          return result;
        }
        case UnaryExpr::U_NOT: {
          ExprResult result;
          result.type = AstType::Int();
          result.value = emitUnaryResult(ir::UnaryOp::Not,
                                         scalarMidType(operand.type), Type::I32(),
                                         operand.value);
          return result;
        }
      }
    }

    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
      if (binary->op == BinaryExpr::B_AND || binary->op == BinaryExpr::B_OR) {
        return genLogicalExpr(binary);
      }

      ExprResult lhs = genExprResult(binary->left.get());
      ExprResult rhs = genExprResult(binary->right.get());
      AstType operandType = commonNumericType(lhs.type, rhs.type);
      lhs = castExprResult(lhs, operandType);
      rhs = castExprResult(rhs, operandType);

      AstType resultType = isRelationalOp(binary->op) ? AstType::Int() : operandType;
      ExprResult result;
      result.type = resultType;
      result.value = emitBinaryResult(
          toBinaryOp(binary->op), scalarMidType(operandType),
          scalarMidType(resultType), lhs.value, rhs.value);
      return result;
    }

    return ExprResult{};
  }

  ExprResult genLogicalExpr(BinaryExpr* node) {
    int resultId = newLegacyValue(Type::I32());
    int trueBlock = newBlock("logic_true");
    int falseBlock = newBlock("logic_false");
    int endBlock = newBlock("logic_end");

    genCond(node, blockName(trueBlock), blockName(falseBlock));

    switchToBlock(trueBlock);
    assignLegacyValue(resultId, Type::I32(), ValueRef::ImmediateInt(1));
    if (!currentBlockTerminated()) emitJump(blockName(endBlock));

    switchToBlock(falseBlock);
    assignLegacyValue(resultId, Type::I32(), ValueRef::ImmediateInt(0));
    if (!currentBlockTerminated()) emitJump(blockName(endBlock));

    switchToBlock(endBlock);
    ExprResult result;
    result.type = AstType::Int();
    result.value = ValueRef::SSA(resultId, Type::I32());
    return result;
  }

  void genCond(Expr* expr, const std::string& trueLabel,
               const std::string& falseLabel) {
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
      if (unary->op == UnaryExpr::U_NOT) {
        genCond(unary->operand.get(), falseLabel, trueLabel);
        return;
      }
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
      if (binary->op == BinaryExpr::B_AND) {
        int midBlock = newBlock("land_rhs");
        genCond(binary->left.get(), blockName(midBlock), falseLabel);
        switchToBlock(midBlock);
        genCond(binary->right.get(), trueLabel, falseLabel);
        return;
      }
      if (binary->op == BinaryExpr::B_OR) {
        int midBlock = newBlock("lor_rhs");
        genCond(binary->left.get(), trueLabel, blockName(midBlock));
        switchToBlock(midBlock);
        genCond(binary->right.get(), trueLabel, falseLabel);
        return;
      }
    }

    ExprResult cond = genExprResult(expr);
    Instruction inst;
    inst.kind = InstKind::Branch;
    inst.operands.push_back(cond.value);
    inst.true_target = trueLabel;
    inst.false_target = falseLabel;
    appendInstruction(std::move(inst));
  }

  ExprResult genArrayAccess(ArrayAccessExpr* node) {
    ExprResult lvalResult = genLValueExpr(node);
    if (!lvalResult.type.isValid()) return ExprResult{};

    if (!lvalResult.type.isArray) {
      ExprResult result;
      result.type = lvalResult.type;
      result.isArrayAccess = true;
      result.addr = lvalResult.addr;
      result.value = emitLoadResult(scalarMidType(result.type), lvalResult.addr);
      return result;
    }

    return lvalResult;
  }

  ExprResult genLValueExpr(Expr* expr) {
    if (auto* id = dynamic_cast<IdentifierExpr*>(expr)) {
      Binding* binding = lookupBinding(id->name);
      if (!binding) return ExprResult{};

      ExprResult result;
      result.type = binding->type.withoutConst();
      if (binding->isArray) {
        result.isArrayAccess = true;
        if (binding->isGlobal) {
          result.addr = ValueRef::Global(binding->global_name, Type::Ptr());
        } else if (binding->isArrayParam) {
          result.addr = ValueRef::SSA(binding->legacy_id, Type::Ptr());
        } else {
          result.addr = ValueRef::Frame(binding->stack_offset);
        }
        result.value = result.addr;
      } else if (binding->isGlobal) {
        result.addr = ValueRef::Global(binding->global_name, Type::Ptr());
        result.value = result.addr;
      } else {
        result.value = ValueRef::SSA(binding->legacy_id, scalarMidType(binding->type));
      }
      return result;
    }

    if (auto* arr = dynamic_cast<ArrayAccessExpr*>(expr)) {
      ExprResult baseResult = genLValueExpr(arr->array.get());
      if (!baseResult.type.isValid() || !baseResult.type.isArray) return ExprResult{};

      ExprResult indexResult = castExprResult(genExprResult(arr->index.get()), AstType::Int());
      int elementCount = 1;
      if (baseResult.type.firstDimUnsized) {
        for (int dim : baseResult.type.arrayDimensions) elementCount *= dim;
      } else if (baseResult.type.arrayDimensions.size() > 1) {
        for (size_t i = 1; i < baseResult.type.arrayDimensions.size(); ++i) {
          elementCount *= baseResult.type.arrayDimensions[i];
        }
      }

      int strideBytes = elementCount * 4;
      ValueRef offset = ValueRef::ImmediateInt(0);
      if (indexResult.value.kind == ValueRef::Kind::ImmediateInt) {
        offset = ValueRef::ImmediateInt(indexResult.value.int_value * strideBytes);
      } else {
        offset = emitBinaryResult(ir::BinaryOp::Mul, Type::I32(), Type::I32(),
                                  indexResult.value,
                                  ValueRef::ImmediateInt(strideBytes));
      }

      ValueRef finalAddr = baseResult.addr;
      if (offset.kind == ValueRef::Kind::ImmediateInt) {
        if (offset.int_value != 0) {
          if (baseResult.addr.kind == ValueRef::Kind::FrameAddress) {
            finalAddr = ValueRef::Frame(baseResult.addr.frame_offset + offset.int_value);
          } else {
            finalAddr = emitBinaryResult(ir::BinaryOp::Add, Type::I32(), Type::Ptr(),
                                         baseResult.addr, offset);
          }
        }
      } else {
        finalAddr = emitBinaryResult(ir::BinaryOp::Add, Type::I32(), Type::Ptr(),
                                     baseResult.addr, offset);
      }

      ExprResult result;
      result.isArrayAccess = true;
      result.addr = finalAddr;
      result.type = arrayElementType(baseResult.type);
      result.value = finalAddr;
      return result;
    }

    return ExprResult{};
  }

  void emitJump(const std::string& target) {
    Instruction inst;
    inst.kind = InstKind::Jump;
    inst.jump_target = target;
    appendInstruction(std::move(inst));
  }

  void emitReturn(const ExprResult* value = nullptr) {
    Instruction inst;
    inst.kind = InstKind::Return;
    inst.has_value = value != nullptr && value->type.base != BaseType::VOID;
    if (inst.has_value) {
      inst.operands.push_back(value->value);
    }
    appendInstruction(std::move(inst));
  }

  void emitDefaultReturn() {
    if (current_function_->return_type == Type::Void()) {
      emitReturn();
      return;
    }
    ExprResult zero;
    if (current_function_->return_type == Type::F32()) {
      zero.type = AstType::Float();
      zero.value = ValueRef::ImmediateFloat(0.0f);
    } else {
      zero.type = AstType::Int();
      zero.value = ValueRef::ImmediateInt(0);
    }
    emitReturn(&zero);
  }

  void genStmt(Stmt* stmt) {
    if (currentBlockTerminated()) return;

    if (auto* blk = dynamic_cast<Block*>(stmt)) {
      genBlock(blk);
      return;
    }
    if (dynamic_cast<EmptyStmt*>(stmt)) return;
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
      genExprResult(exprStmt->expr.get());
      return;
    }
    if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
      ExprResult lvalResult = genLValueExpr(assign->lvalue.get());
      if (!lvalResult.type.isValid()) return;
      ExprResult value =
          castExprResult(genExprResult(assign->value.get()),
                         lvalResult.type.withoutConst());
      if (lvalResult.isArrayAccess) {
        emitStore(value.value, lvalResult.addr);
      } else if (lvalResult.value.kind == ValueRef::Kind::GlobalSymbol) {
        emitStore(value.value, lvalResult.value);
      } else if (lvalResult.value.isSSA()) {
        assignLegacyValue(lvalResult.value.value_id, scalarMidType(lvalResult.type),
                          value.value);
      }
      return;
    }
    if (auto* decl = dynamic_cast<VarDeclStmt*>(stmt)) {
      emitDecl(decl, false);
      return;
    }
    if (auto* ifs = dynamic_cast<IfStmt*>(stmt)) {
      int thenBlock = newBlock("if_true");
      int endBlock = newBlock("if_end");
      if (ifs->elseStmt) {
        int elseBlock = newBlock("if_false");
        genCond(ifs->condition.get(), blockName(thenBlock), blockName(elseBlock));
        switchToBlock(thenBlock);
        genStmt(ifs->thenStmt.get());
        if (!currentBlockTerminated()) emitJump(blockName(endBlock));
        switchToBlock(elseBlock);
        genStmt(ifs->elseStmt.get());
        if (!currentBlockTerminated()) emitJump(blockName(endBlock));
      } else {
        genCond(ifs->condition.get(), blockName(thenBlock), blockName(endBlock));
        switchToBlock(thenBlock);
        genStmt(ifs->thenStmt.get());
        if (!currentBlockTerminated()) emitJump(blockName(endBlock));
      }
      switchToBlock(endBlock);
      return;
    }
    if (auto* wh = dynamic_cast<WhileStmt*>(stmt)) {
      int condBlock = newBlock("while_cond");
      int bodyBlock = newBlock("while_body");
      int endBlock = newBlock("while_end");
      emitJump(blockName(condBlock));
      switchToBlock(condBlock);
      genCond(wh->condition.get(), blockName(bodyBlock), blockName(endBlock));
      loop_stack_.push_back({blockName(endBlock), blockName(condBlock)});
      switchToBlock(bodyBlock);
      genStmt(wh->body.get());
      loop_stack_.pop_back();
      if (!currentBlockTerminated()) emitJump(blockName(condBlock));
      switchToBlock(endBlock);
      return;
    }
    if (dynamic_cast<BreakStmt*>(stmt)) {
      if (!loop_stack_.empty()) emitJump(loop_stack_.back().first);
      return;
    }
    if (dynamic_cast<ContinueStmt*>(stmt)) {
      if (!loop_stack_.empty()) emitJump(loop_stack_.back().second);
      return;
    }
    if (auto* ret = dynamic_cast<ReturnStmt*>(stmt)) {
      if (ret->returnValue) {
        AstType targetType = current_function_->return_type == Type::F32()
                                 ? AstType::Float()
                                 : AstType::Int();
        ExprResult value = castExprResult(genExprResult(ret->returnValue.get()),
                                          targetType);
        emitReturn(&value);
      } else {
        emitReturn();
      }
    }
  }

  void genBlock(Block* block) {
    enterScope();
    for (const auto& stmt : block->stmts) {
      if (currentBlockTerminated()) break;
      genStmt(stmt.get());
    }
    exitScope();
  }

  Function buildFunction(FuncDef* func) {
    Function function;
    function.name = func->name;
    function.return_type = scalarMidType(func->returnType);
    current_function_ = &function;
    current_block_index_ = -1;
    block_counter_ = 0;
    loop_stack_.clear();

    enterScope();
    int entryBlock = newBlock("entry");
    switchToBlock(entryBlock);

    for (const auto& paramPtr : func->params) {
      Binding binding;
      binding.type = paramPtr->type;
      binding.isArray = paramPtr->type.isArray;
      binding.isArrayParam = paramPtr->type.isArray;
      binding.legacy_id = newLegacyValue(paramMidType(paramPtr->type));
      declareLocalValue(paramPtr->name, binding);
      function.param_source_ids.push_back(binding.legacy_id);
      function.param_types.push_back(paramMidType(paramPtr->type));
      function.param_is_array.push_back(paramPtr->type.isArray);
    }

    genBlock(func->body.get());
    if (!currentBlockTerminated()) {
      emitDefaultReturn();
    }
    exitScope();

    finalizeFunctionSSA(function);
    current_function_ = nullptr;
    current_block_index_ = -1;
    return function;
  }
};

}  // namespace

Module MidIRBuilder::build(const ir::IRProgram& bridgeProgram) const {
  Module module;
  for (const auto& global : bridgeProgram.globals) {
    module.globals.push_back(GlobalVar{global.name, global.typedInitialValue,
                                       toMidIRType(global.valueType), global.isConst});
  }
  for (const auto& array : bridgeProgram.globalArrays) {
    module.global_arrays.push_back(GlobalArray{array.name, array.dimensions,
                                               toMidIRType(array.elementType),
                                               array.initialValues, array.isConst});
  }
  for (const auto& function : bridgeProgram.functions) {
    module.functions.push_back(buildFunctionFromBridge(function));
  }
  return module;
}

Module MidIRBuilder::build(CompUnit* root) const {
  DirectMidIRBuilder builder;
  return builder.build(root);
}

}  // namespace midir

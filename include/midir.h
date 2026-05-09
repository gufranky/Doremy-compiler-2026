#ifndef MIDIR_H
#define MIDIR_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ir.h"

namespace midir {

enum class TypeKind { Void, I1, I32, F32, Ptr };

struct Type {
  TypeKind kind = TypeKind::Void;

  static Type Void() { return Type{TypeKind::Void}; }
  static Type I1() { return Type{TypeKind::I1}; }
  static Type I32() { return Type{TypeKind::I32}; }
  static Type F32() { return Type{TypeKind::F32}; }
  static Type Ptr() { return Type{TypeKind::Ptr}; }

  bool operator==(const Type& other) const { return kind == other.kind; }
  bool operator!=(const Type& other) const { return !(*this == other); }
};

struct Operand {
  enum class Kind {
    Invalid,
    Register,
    ConstInt,
    ConstFloat,
    Global,
    Frame,
    StackPtr,
  };

  Kind kind = Kind::Invalid;
  Type type = Type::Void();
  int reg = -1;
  int intValue = 0;
  float floatValue = 0.0f;
  int frameOffset = 0;
  std::string symbol;

  static Operand Invalid() { return Operand{}; }
  static Operand Reg(int id, Type t) {
    Operand op;
    op.kind = Kind::Register;
    op.type = t;
    op.reg = id;
    return op;
  }
  static Operand ConstInt(int value) {
    Operand op;
    op.kind = Kind::ConstInt;
    op.type = Type::I32();
    op.intValue = value;
    return op;
  }
  static Operand ConstFloat(float value) {
    Operand op;
    op.kind = Kind::ConstFloat;
    op.type = Type::F32();
    op.floatValue = value;
    return op;
  }
  static Operand Global(std::string name, Type t = Type::Ptr()) {
    Operand op;
    op.kind = Kind::Global;
    op.type = t;
    op.symbol = std::move(name);
    return op;
  }
  static Operand Frame(int offset) {
    Operand op;
    op.kind = Kind::Frame;
    op.type = Type::Ptr();
    op.frameOffset = offset;
    return op;
  }
  static Operand StackPtr() {
    Operand op;
    op.kind = Kind::StackPtr;
    op.type = Type::Ptr();
    return op;
  }

  bool isValid() const { return kind != Kind::Invalid; }
  bool isRegister() const { return kind == Kind::Register; }
  bool isConstInt() const { return kind == Kind::ConstInt; }
  bool isConstFloat() const { return kind == Kind::ConstFloat; }
  bool isConst() const { return isConstInt() || isConstFloat(); }
  bool isGlobal() const { return kind == Kind::Global; }
  bool isFrame() const { return kind == Kind::Frame; }
  bool isStackPtr() const { return kind == Kind::StackPtr; }
  bool isPointerLike() const {
    return isGlobal() || isFrame() || isStackPtr() || type.kind == TypeKind::Ptr;
  }
};

enum class InstKind {
  Binary,
  Unary,
  Cmp,
  Cast,
  Copy,
  Phi,
  Load,
  Store,
  FrameAddr,
  GlobalAddr,
  GEP,
  Select,
  Call,
  Branch,
  Jump,
  Return,
};

struct Instruction {
  InstKind kind = InstKind::Copy;
  Type type = Type::Void();
  Type operandType = Type::Void();
  int dest = -1;
  int sourceReg = -1;
  ir::ValueType lirValueType = ir::ValueType::I32;
  ir::ValueType lirOperandType = ir::ValueType::I32;
  ir::ValueType lirResultType = ir::ValueType::I32;
  ir::BinaryOp binaryOp = ir::BinaryOp::Add;
  ir::UnaryOp unaryOp = ir::UnaryOp::Plus;
  std::vector<Operand> operands;
  std::vector<std::pair<int, Operand>> incomings;
  std::vector<Type> callArgTypes;
  std::string symbol;
  std::string trueTarget;
  std::string falseTarget;
  std::string jumpTarget;
  int frameOffset = 0;
  int gepOffset = 0;
  Operand gepIndex = Operand::Invalid();
  bool hasResult = false;
  bool hasValue = false;
  bool isPureCall = false;
};

struct BasicBlock {
  std::string name;
  std::vector<Instruction> instructions;
  std::vector<int> preds;
  std::vector<int> succs;
};

struct ValueInfo {
  Type type = Type::Void();
  std::vector<int> users;
};

enum class ObjectKind {
  Unknown,
  ParamScalar,
  LocalScalar,
  TempScalar,
  Aggregate,
  AddressTaken,
  GlobalMemory,
};

struct VariableObject {
  int id = -1;
  ObjectKind kind = ObjectKind::Unknown;
  std::string name;
  Type type = Type::Void();
  ir::ValueType lirType = ir::ValueType::I32;
  int baseReg = -1;
  int frameOffset = 0;
  bool renameable = false;
  bool addressEscapes = false;
};

struct Function {
  std::string name;
  std::vector<BasicBlock> blocks;
  std::unordered_map<std::string, int> blockIndexByName;
  int entryBlock = -1;
  std::vector<int> params;
  std::vector<Type> paramTypes;
  std::vector<bool> paramIsArray;
  Type returnType = Type::Void();
  int localArraySize = 0;
  int nextValueId = 0;
  int nextObjectId = 0;
  bool inSSA = false;
  std::vector<ValueInfo> values;
  std::unordered_map<int, VariableObject> frameObjects;
  std::unordered_map<int, VariableObject> registerObjects;

  int allocateValue(Type type);
  int allocateObject();
};

struct GlobalVar {
  std::string name;
  ScalarValue initialValue = ScalarValue::Int(0);
  Type type = Type::I32();
  bool isConst = false;
};

struct GlobalArray {
  std::string name;
  std::vector<int> dimensions;
  Type elementType = Type::I32();
  std::vector<ScalarValue> initialValues;
  bool isConst = false;
};

struct Module {
  std::vector<GlobalVar> globals;
  std::vector<GlobalArray> globalArrays;
  std::vector<Function> functions;
};

struct DominatorTree {
  std::vector<int> idom;
  std::vector<std::vector<int>> children;
  std::vector<std::vector<int>> frontier;
};

struct Loop {
  int header = -1;
  int preheader = -1;
  int latch = -1;
  std::vector<int> blocks;
  std::vector<int> exitBlocks;
};

struct LoopInfo {
  std::vector<Loop> loops;
};

struct MemoryAccess {
  enum class Kind { LiveOnEntry, Phi, Def, Use };

  Kind kind = Kind::LiveOnEntry;
  int id = -1;
  int block = -1;
  int instIndex = -1;
  std::vector<int> inputs;
  int incoming = -1;
};

struct MemorySSA {
  std::vector<MemoryAccess> accesses;
  std::vector<int> blockLiveIn;
  std::vector<int> blockLiveOut;
  std::unordered_map<long long, int> instToAccess;
};

Module BuildMidIR(const ir::IRProgram& lirProgram);
ir::IRProgram LowerMidToLIR(const Module& module);

bool VerifyMidIR(const Module& module, std::string* error = nullptr);
bool VerifyMidFunction(const Function& function, std::string* error = nullptr);

DominatorTree BuildDominatorTree(const Function& function);
LoopInfo BuildLoopInfo(const Function& function, const DominatorTree& domTree);
MemorySSA BuildMemorySSA(const Function& function, const DominatorTree& domTree);

void ConvertToSSA(Function& function);
void RebuildUseLists(Function& function);
void OptimizeMidProgram(Module& module);

Type ToMidType(ir::ValueType valueType, bool forcePtr = false);
ir::ValueType ToLIRValueType(Type type);

}  // namespace midir

#endif  // MIDIR_H

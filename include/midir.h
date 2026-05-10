#ifndef MIDIR_H
#define MIDIR_H

#include <string>
#include <unordered_map>
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

struct ValueRef {
  enum class Kind {
    Invalid,
    Undef,
    SSA,
    ImmediateInt,
    ImmediateFloat,
    GlobalSymbol,
    FrameAddress,
    StackPointer,
  };

  Kind kind = Kind::Invalid;
  Type type = Type::Void();
  int value_id = -1;
  int int_value = 0;
  float float_value = 0.0f;
  int frame_offset = 0;
  std::string symbol;

  static ValueRef Invalid() { return ValueRef{}; }
  static ValueRef Undef(Type valueType) {
    ValueRef ref;
    ref.kind = Kind::Undef;
    ref.type = valueType;
    return ref;
  }
  static ValueRef SSA(int valueId, Type valueType) {
    ValueRef ref;
    ref.kind = Kind::SSA;
    ref.type = valueType;
    ref.value_id = valueId;
    return ref;
  }
  static ValueRef ImmediateInt(int value) {
    ValueRef ref;
    ref.kind = Kind::ImmediateInt;
    ref.type = Type::I32();
    ref.int_value = value;
    return ref;
  }
  static ValueRef ImmediateFloat(float value) {
    ValueRef ref;
    ref.kind = Kind::ImmediateFloat;
    ref.type = Type::F32();
    ref.float_value = value;
    return ref;
  }
  static ValueRef Global(std::string name, Type valueType = Type::Ptr()) {
    ValueRef ref;
    ref.kind = Kind::GlobalSymbol;
    ref.type = valueType;
    ref.symbol = std::move(name);
    return ref;
  }
  static ValueRef Frame(int offset) {
    ValueRef ref;
    ref.kind = Kind::FrameAddress;
    ref.type = Type::Ptr();
    ref.frame_offset = offset;
    return ref;
  }
  static ValueRef StackPtr() {
    ValueRef ref;
    ref.kind = Kind::StackPointer;
    ref.type = Type::Ptr();
    return ref;
  }

  bool isValid() const { return kind != Kind::Invalid; }
  bool isUndef() const { return kind == Kind::Undef; }
  bool isSSA() const { return kind == Kind::SSA; }
  bool isImmediate() const {
    return kind == Kind::ImmediateInt || kind == Kind::ImmediateFloat;
  }
  bool isPointerLike() const {
    return kind == Kind::GlobalSymbol || kind == Kind::FrameAddress ||
           kind == Kind::StackPointer || type.kind == TypeKind::Ptr;
  }
};

enum class InstKind {
  Binary,
  Unary,
  Copy,
  Phi,
  Load,
  Store,
  Call,
  Branch,
  Jump,
  Return,
};

struct PhiIncoming {
  int pred_block = -1;
  ValueRef value = ValueRef::Invalid();
};

struct Instruction {
  InstKind kind = InstKind::Copy;
  Type result_type = Type::Void();
  int result_id = -1;
  int legacy_result_id = -1;
  ir::BinaryOp binary_op = ir::BinaryOp::Add;
  ir::UnaryOp unary_op = ir::UnaryOp::Plus;
  Type operand_type = Type::Void();
  std::vector<ValueRef> operands;
  std::vector<PhiIncoming> incomings;
  std::vector<Type> call_arg_types;
  std::string callee;
  std::string true_target;
  std::string false_target;
  std::string jump_target;
  bool has_result = false;
  bool has_value = false;
};

struct BasicBlock {
  std::string name;
  std::vector<Instruction> instructions;
  std::vector<int> preds;
  std::vector<int> succs;

  bool hasTerminator() const;
};

struct GlobalVar {
  std::string name;
  ScalarValue initial_value = ScalarValue::Int(0);
  Type value_type = Type::I32();
  bool is_const = false;
};

struct GlobalArray {
  std::string name;
  std::vector<int> dimensions;
  Type element_type = Type::I32();
  std::vector<ScalarValue> initial_values;
  bool is_const = false;
};

struct Function {
  std::string name;
  std::vector<BasicBlock> blocks;
  std::unordered_map<std::string, int> block_index_by_name;
  int entry_block = -1;
  std::vector<int> params;
  std::vector<int> param_source_ids;
  std::vector<Type> param_types;
  std::vector<bool> param_is_array;
  Type return_type = Type::Void();
  int local_array_size = 0;
  int next_value_id = 0;
  int legacy_vreg_count = 0;
  bool in_ssa = false;
  std::vector<Type> value_types;
  std::vector<Type> legacy_value_types;

  int newValue(Type type);
};

struct Module {
  std::vector<GlobalVar> globals;
  std::vector<GlobalArray> global_arrays;
  std::vector<Function> functions;
};

Type toMidIRType(ir::ValueType valueType, bool forcePtr = false);
ir::ValueType toBridgeIRType(Type type);
std::string typeToString(Type type);
std::string valueToString(const ValueRef& value);
std::string dumpFunction(const Function& function);
std::string dumpModule(const Module& module);

}  // namespace midir

#endif

#ifndef IR_H
#define IR_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ast.h"

namespace ir {

enum class ValueType { I32, F32 };

struct Operand {
  enum class Kind { Immediate, VirtualRegister, GlobalVariable };

  Kind kind = Kind::Immediate;
  ValueType valueType = ValueType::I32;
  int immValue = 0;
  float immFloatValue = 0.0f;
  int vregId = -1;
  std::string globalName;

  static Operand Imm(int value);
  static Operand Imm(float value);
  static Operand Imm(const ScalarValue& value);
  static Operand VReg(int id, ValueType type = ValueType::I32);
  static Operand Global(std::string name, ValueType type = ValueType::I32);

  bool isImm() const { return kind == Kind::Immediate; }
  bool isVReg() const { return kind == Kind::VirtualRegister; }
  bool isGlobal() const { return kind == Kind::GlobalVariable; }
  bool isIntImm() const { return isImm() && valueType == ValueType::I32; }
  bool isFloatImm() const { return isImm() && valueType == ValueType::F32; }

  ScalarValue scalarValue() const {
    return valueType == ValueType::F32 ? ScalarValue::Float(immFloatValue)
                                       : ScalarValue::Int(immValue);
  }
};

enum class InstKind {
  Binary,
  Unary,
  Copy,
  Load,
  Store,
  Branch,
  Jump,
  Call,
  Return,
  Label
};

enum class BinaryOp {
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  And,
  Or,
  Lt,
  Gt,
  Le,
  Ge,
  Eq,
  Ne
};

enum class UnaryOp { Neg, Not, Plus };

struct Instruction {
  explicit Instruction(InstKind k) : kind(k) {}
  virtual ~Instruction() = default;
  InstKind kind;
};

struct BinaryInst : public Instruction {
  BinaryOp op;
  ValueType operandType;
  ValueType resultType;
  int dest;
  Operand lhs;
  Operand rhs;

  BinaryInst(BinaryOp op, int dest, Operand lhs, Operand rhs)
      : BinaryInst(op, ValueType::I32, ValueType::I32, dest, std::move(lhs),
                   std::move(rhs)) {}

  BinaryInst(BinaryOp op, ValueType operandType, ValueType resultType, int dest,
             Operand lhs, Operand rhs)
      : Instruction(InstKind::Binary),
        op(op),
        operandType(operandType),
        resultType(resultType),
        dest(dest),
        lhs(std::move(lhs)),
        rhs(std::move(rhs)) {}
};

struct UnaryInst : public Instruction {
  UnaryOp op;
  ValueType operandType;
  ValueType resultType;
  int dest;
  Operand operand;

  UnaryInst(UnaryOp op, int dest, Operand operand)
      : UnaryInst(op, operand.valueType, operand.valueType, dest,
                  std::move(operand)) {}

  UnaryInst(UnaryOp op, ValueType operandType, ValueType resultType, int dest,
            Operand operand)
      : Instruction(InstKind::Unary),
        op(op),
        operandType(operandType),
        resultType(resultType),
        dest(dest),
        operand(std::move(operand)) {}
};

struct CopyInst : public Instruction {
  ValueType destType;
  int dest;
  Operand src;

  CopyInst(int dest, Operand src)
      : CopyInst(src.valueType, dest, std::move(src)) {}

  CopyInst(ValueType destType, int dest, Operand src)
      : Instruction(InstKind::Copy),
        destType(destType),
        dest(dest),
        src(std::move(src)) {}
};

struct LoadInst : public Instruction {
  ValueType valueType;
  int dest;
  Operand addr;

  LoadInst(int dest, Operand addr)
      : LoadInst(ValueType::I32, dest, std::move(addr)) {}

  LoadInst(ValueType valueType, int dest, Operand addr)
      : Instruction(InstKind::Load),
        valueType(valueType),
        dest(dest),
        addr(std::move(addr)) {}
};

struct StoreInst : public Instruction {
  ValueType valueType;
  Operand src;
  Operand addr;

  StoreInst(Operand src, Operand addr)
      : StoreInst(src.valueType, std::move(src), std::move(addr)) {}

  StoreInst(ValueType valueType, Operand src, Operand addr)
      : Instruction(InstKind::Store),
        valueType(valueType),
        src(std::move(src)),
        addr(std::move(addr)) {}
};

struct BranchInst : public Instruction {
  Operand cond;
  std::string trueLabel;
  std::string falseLabel;
  BranchInst(Operand cond, std::string trueLabel, std::string falseLabel)
      : Instruction(InstKind::Branch),
        cond(std::move(cond)),
        trueLabel(std::move(trueLabel)),
        falseLabel(std::move(falseLabel)) {}
};

struct JumpInst : public Instruction {
  std::string target;
  explicit JumpInst(std::string target)
      : Instruction(InstKind::Jump), target(std::move(target)) {}
};

struct CallInst : public Instruction {
  bool hasDest;
  int dest;
  ValueType resultType;
  std::string callee;
  std::vector<Operand> args;
  std::vector<ValueType> argTypes;

  CallInst(std::string callee, std::vector<Operand> args)
      : CallInst(std::move(callee), std::move(args), {}) {}

  CallInst(std::string callee, std::vector<Operand> args,
           std::vector<ValueType> argTypes)
      : Instruction(InstKind::Call),
        hasDest(false),
        dest(-1),
        resultType(ValueType::I32),
        callee(std::move(callee)),
        args(std::move(args)),
        argTypes(std::move(argTypes)) {}

  CallInst(int dest, std::string callee, std::vector<Operand> args)
      : CallInst(ValueType::I32, dest, std::move(callee), std::move(args), {}) {}

  CallInst(ValueType resultType, int dest, std::string callee,
           std::vector<Operand> args)
      : CallInst(resultType, dest, std::move(callee), std::move(args), {}) {}

  CallInst(ValueType resultType, int dest, std::string callee,
           std::vector<Operand> args, std::vector<ValueType> argTypes)
      : Instruction(InstKind::Call),
        hasDest(true),
        dest(dest),
        resultType(resultType),
        callee(std::move(callee)),
        args(std::move(args)),
        argTypes(std::move(argTypes)) {}
};

struct ReturnInst : public Instruction {
  bool hasValue;
  ValueType valueType;
  Operand value;

  ReturnInst()
      : Instruction(InstKind::Return),
        hasValue(false),
        valueType(ValueType::I32),
        value(Operand::Imm(0)) {}

  explicit ReturnInst(Operand value)
      : ReturnInst(value.valueType, std::move(value)) {}

  ReturnInst(ValueType valueType, Operand value)
      : Instruction(InstKind::Return),
        hasValue(true),
        valueType(valueType),
        value(std::move(value)) {}
};

struct LabelInst : public Instruction {
  std::string label;
  explicit LabelInst(std::string label)
      : Instruction(InstKind::Label), label(std::move(label)) {}
};

struct IRFunction {
  std::string name;
  std::vector<std::unique_ptr<Instruction>> instructions;
  int nextVReg = 0;
  std::vector<int> params;
  std::vector<ValueType> paramTypes;
  ValueType returnType = ValueType::I32;

  explicit IRFunction(std::string name) : name(std::move(name)) {}

  IRFunction(const IRFunction&) = delete;
  IRFunction& operator=(const IRFunction&) = delete;
  IRFunction(IRFunction&&) = default;
  IRFunction& operator=(IRFunction&&) = default;

  int newVReg() { return nextVReg++; }

  template <typename T, typename... Args>
  T* append(Args&&... args) {
    auto node = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = node.get();
    instructions.emplace_back(std::move(node));
    return raw;
  }
};

struct GlobalVar {
  std::string name;
  int initialValue = 0;
  ScalarValue typedInitialValue = ScalarValue::Int(0);
  ValueType valueType = ValueType::I32;
  bool isConst = false;

  GlobalVar() = default;

  GlobalVar(std::string n, int init, bool isConstValue)
      : name(std::move(n)),
        initialValue(init),
        typedInitialValue(ScalarValue::Int(init)),
        valueType(ValueType::I32),
        isConst(isConstValue) {}

  GlobalVar(std::string n, ScalarValue init, bool isConstValue)
      : name(std::move(n)),
        initialValue(init.intValue),
        typedInitialValue(init),
        valueType(init.isFloat() ? ValueType::F32 : ValueType::I32),
        isConst(isConstValue) {}
};

struct IRProgram {
  std::vector<GlobalVar> globals;
  std::vector<IRFunction> functions;

  IRProgram() = default;
  IRProgram(const IRProgram&) = delete;
  IRProgram& operator=(const IRProgram&) = delete;
  IRProgram(IRProgram&&) = default;
  IRProgram& operator=(IRProgram&&) = default;
};

}  // namespace ir

#endif

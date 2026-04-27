#ifndef IR_H
#define IR_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ir {

// Operand for IR instructions
struct Operand {
  enum class Kind { Immediate, VirtualRegister, GlobalVariable };

  Kind kind;
  int immValue;
  int vregId;
  std::string globalName;

  static Operand Imm(int value);
  static Operand VReg(int id);
  static Operand Global(std::string name);

  bool isImm() const { return kind == Kind::Immediate; }
  bool isVReg() const { return kind == Kind::VirtualRegister; }
  bool isGlobal() const { return kind == Kind::GlobalVariable; }
};

// Instruction kinds
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

// Operation enums
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

// Base instruction
struct Instruction {
  explicit Instruction(InstKind k) : kind(k) {}
  virtual ~Instruction() = default;
  InstKind kind;
};

struct BinaryInst : public Instruction {
  BinaryOp op;
  int dest;  // virtual register id
  Operand lhs;
  Operand rhs;
  BinaryInst(BinaryOp op, int dest, Operand lhs, Operand rhs)
      : Instruction(InstKind::Binary),
        op(op),
        dest(dest),
        lhs(std::move(lhs)),
        rhs(std::move(rhs)) {}
};

struct UnaryInst : public Instruction {
  UnaryOp op;
  int dest;
  Operand operand;
  UnaryInst(UnaryOp op, int dest, Operand operand)
      : Instruction(InstKind::Unary),
        op(op),
        dest(dest),
        operand(std::move(operand)) {}
};

struct CopyInst : public Instruction {
  int dest;
  Operand src;
  CopyInst(int dest, Operand src)
      : Instruction(InstKind::Copy), dest(dest), src(std::move(src)) {}
};

struct LoadInst : public Instruction {
  int dest;
  Operand addr;
  LoadInst(int dest, Operand addr)
      : Instruction(InstKind::Load), dest(dest), addr(std::move(addr)) {}
};

struct StoreInst : public Instruction {
  Operand src;
  Operand addr;
  StoreInst(Operand src, Operand addr)
      : Instruction(InstKind::Store),
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
  int dest;  // valid only if hasDest
  std::string callee;
  std::vector<Operand> args;

  CallInst(std::string callee, std::vector<Operand> args)
      : Instruction(InstKind::Call),
        hasDest(false),
        dest(-1),
        callee(std::move(callee)),
        args(std::move(args)) {}

  CallInst(int dest, std::string callee, std::vector<Operand> args)
      : Instruction(InstKind::Call),
        hasDest(true),
        dest(dest),
        callee(std::move(callee)),
        args(std::move(args)) {}
};

struct ReturnInst : public Instruction {
  bool hasValue;
  Operand value;

  ReturnInst()
      : Instruction(InstKind::Return),
        hasValue(false),
        value(Operand::Imm(0)) {}

  explicit ReturnInst(Operand value)
      : Instruction(InstKind::Return),
        hasValue(true),
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
  std::vector<int> params;  // virtual registers for parameters (in order)

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

struct IRProgram {
  std::vector<IRFunction> functions;

  IRProgram() = default;
  IRProgram(const IRProgram&) = delete;
  IRProgram& operator=(const IRProgram&) = delete;
  IRProgram(IRProgram&&) = default;
  IRProgram& operator=(IRProgram&&) = default;
};

}  // namespace ir

#endif  // IR_H

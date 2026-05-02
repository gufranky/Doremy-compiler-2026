#include <cstring>

#include "ir.h"

namespace ir {

Operand Operand::Imm(int value) {
  Operand op;
  op.kind = Kind::Immediate;
  op.valueType = ValueType::I32;
  op.immValue = value;
  op.immFloatValue = static_cast<float>(value);
  op.vregId = -1;
  return op;
}

Operand Operand::Imm(float value) {
  Operand op;
  op.kind = Kind::Immediate;
  op.valueType = ValueType::F32;
  static_assert(sizeof(float) == sizeof(int), "float/int size mismatch");
  std::memcpy(&op.immValue, &value, sizeof(value));
  op.immFloatValue = value;
  op.vregId = -1;
  return op;
}

Operand Operand::Imm(const ScalarValue& value) {
  return value.isFloat() ? Imm(value.floatValue) : Imm(value.intValue);
}

Operand Operand::VReg(int id, ValueType type) {
  Operand op;
  op.kind = Kind::VirtualRegister;
  op.valueType = type;
  op.immValue = 0;
  op.immFloatValue = 0.0f;
  op.vregId = id;
  return op;
}

Operand Operand::Global(std::string name, ValueType type) {
  Operand op;
  op.kind = Kind::GlobalVariable;
  op.valueType = type;
  op.immValue = 0;
  op.immFloatValue = 0.0f;
  op.vregId = -1;
  op.globalName = std::move(name);
  return op;
}

Operand Operand::StackPtr() {
  Operand op;
  op.kind = Kind::StackPointer;
  op.valueType = ValueType::I32;
  op.immValue = 0;
  op.immFloatValue = 0.0f;
  op.vregId = -1;
  return op;
}

Operand Operand::LocalVarAddr(int offset) {
  Operand op;
  op.kind = Kind::LocalVarAddress;
  op.valueType = ValueType::I32;
  op.immValue = offset;
  op.immFloatValue = 0.0f;
  op.vregId = -1;
  return op;
}

}  // namespace ir

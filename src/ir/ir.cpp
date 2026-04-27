#include "ir.h"

namespace ir {

Operand Operand::Imm(int value) {
  Operand op;
  op.kind = Kind::Immediate;
  op.immValue = value;
  op.vregId = -1;
  return op;
}

Operand Operand::VReg(int id) {
  Operand op;
  op.kind = Kind::VirtualRegister;
  op.immValue = 0;
  op.vregId = id;
  return op;
}

Operand Operand::Global(std::string name) {
  Operand op;
  op.kind = Kind::GlobalVariable;
  op.immValue = 0;
  op.vregId = -1;
  op.globalName = std::move(name);
  return op;
}

}  // namespace ir

#include "midir.h"

#include <sstream>

namespace midir {

namespace {

std::string joinValues(const std::vector<ValueRef>& values) {
  std::ostringstream oss;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i != 0) oss << ", ";
    oss << valueToString(values[i]);
  }
  return oss.str();
}

std::string dumpInstruction(const Instruction& inst) {
  std::ostringstream oss;
  if (inst.has_result) {
    oss << "%" << inst.result_id << " = ";
  }

  switch (inst.kind) {
    case InstKind::Binary:
      oss << "binary " << static_cast<int>(inst.binary_op) << " "
          << joinValues(inst.operands);
      break;
    case InstKind::Unary:
      oss << "unary " << static_cast<int>(inst.unary_op) << " "
          << joinValues(inst.operands);
      break;
    case InstKind::Copy:
      oss << "copy " << joinValues(inst.operands);
      break;
    case InstKind::Phi:
      oss << "phi ";
      for (size_t i = 0; i < inst.incomings.size(); ++i) {
        if (i != 0) oss << ", ";
        oss << "[bb" << inst.incomings[i].pred_block << ": "
            << valueToString(inst.incomings[i].value) << "]";
      }
      break;
    case InstKind::Load:
      oss << "load " << joinValues(inst.operands);
      break;
    case InstKind::Store:
      oss << "store " << joinValues(inst.operands);
      break;
    case InstKind::Call:
      oss << "call @" << inst.callee << "(" << joinValues(inst.operands) << ")";
      break;
    case InstKind::Branch:
      oss << "br " << joinValues(inst.operands) << " ? " << inst.true_target
          << " : " << inst.false_target;
      break;
    case InstKind::Jump:
      oss << "jmp " << inst.jump_target;
      break;
    case InstKind::Return:
      if (inst.has_value) {
        oss << "ret " << joinValues(inst.operands);
      } else {
        oss << "ret";
      }
      break;
  }
  return oss.str();
}

}  // namespace

bool BasicBlock::hasTerminator() const {
  if (instructions.empty()) return false;
  InstKind kind = instructions.back().kind;
  return kind == InstKind::Branch || kind == InstKind::Jump ||
         kind == InstKind::Return;
}

int Function::newValue(Type type) {
  int id = next_value_id++;
  if (static_cast<int>(value_types.size()) <= id) {
    value_types.resize(id + 1, Type::Void());
  }
  value_types[id] = type;
  return id;
}

Type toMidIRType(ir::ValueType valueType, bool forcePtr) {
  if (forcePtr) return Type::Ptr();
  return valueType == ir::ValueType::F32 ? Type::F32() : Type::I32();
}

ir::ValueType toBridgeIRType(Type type) {
  return type.kind == TypeKind::F32 ? ir::ValueType::F32 : ir::ValueType::I32;
}

std::string typeToString(Type type) {
  switch (type.kind) {
    case TypeKind::Void:
      return "void";
    case TypeKind::I1:
      return "i1";
    case TypeKind::I32:
      return "i32";
    case TypeKind::F32:
      return "f32";
    case TypeKind::Ptr:
      return "ptr";
  }
  return "unknown";
}

std::string valueToString(const ValueRef& value) {
  std::ostringstream oss;
  switch (value.kind) {
    case ValueRef::Kind::Invalid:
      return "<invalid>";
    case ValueRef::Kind::Undef:
      return "undef";
    case ValueRef::Kind::SSA:
      oss << "%" << value.value_id;
      break;
    case ValueRef::Kind::ImmediateInt:
      oss << value.int_value;
      break;
    case ValueRef::Kind::ImmediateFloat:
      oss << value.float_value;
      break;
    case ValueRef::Kind::GlobalSymbol:
      oss << "@" << value.symbol;
      break;
    case ValueRef::Kind::FrameAddress:
      oss << "frame(" << value.frame_offset << ")";
      break;
    case ValueRef::Kind::StackPointer:
      oss << "sp";
      break;
  }
  return oss.str();
}

std::string dumpFunction(const Function& function) {
  std::ostringstream oss;
  oss << "function " << function.name << " -> "
      << typeToString(function.return_type) << "\n";
  for (size_t i = 0; i < function.blocks.size(); ++i) {
    const auto& block = function.blocks[i];
    oss << block.name << ":\n";
    for (const auto& inst : block.instructions) {
      oss << "  " << dumpInstruction(inst) << "\n";
    }
    if (!block.succs.empty()) {
      oss << "  succs:";
      for (int succ : block.succs) {
        oss << " " << function.blocks[succ].name;
      }
      oss << "\n";
    }
  }
  return oss.str();
}

std::string dumpModule(const Module& module) {
  std::ostringstream oss;
  for (const auto& function : module.functions) {
    oss << dumpFunction(function) << "\n";
  }
  return oss.str();
}

}  // namespace midir

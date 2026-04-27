#ifndef BACKEND_CODEGEN_H
#define BACKEND_CODEGEN_H

#include <string>
#include <unordered_map>
#include <vector>

#include "backend_liveness.h"
#include "backend_regalloc.h"
#include "ir.h"

// Code generator with register allocation and stack frame layout.
class CodeGen {
 public:
  // Generate complete assembly with register allocation.
  std::vector<std::string> generate(const ir::IRProgram& program);

 private:
  // Stack frame information for a function.
  struct StackFrame {
    int frameSize;          // Total frame size in bytes (aligned to 16).
    int localVarOffset;     // Offset for local variables.
    int spillAreaSize;      // Size of spill area.
    int spillAreaOffset;    // Offset base for spilled registers.
    int callerSavedOffset;  // Offset base for caller-saved saves.
    int outgoingArgOffset;  // Offset base for outgoing stack args.
    int raOffset;           // Offset of saved return address.
    int savedRegsOffset;    // Offset base for callee-saved saves.
    int maxOutgoingArgs;    // Max stack args beyond a0-a7.

    std::vector<std::string> savedRegs;  // Callee-saved registers.
    std::vector<std::string>
        callerSavedRegs;  // Caller-saved that must be preserved across calls.
    std::unordered_map<int, int> spillSlots;  // vreg -> stack offset.
    std::unordered_map<std::string, int> callerSavedSlots;  // reg -> offset.
  };

  // Render operand with physical register allocation.
  std::string renderOperandWithAlloc(
      const ir::Operand& op, const std::unordered_map<int, int>& allocation,
      const StackFrame& frame) const;

  // Render instruction with physical registers.
  std::string renderInstructionWithAlloc(
      const ir::Instruction* inst,
      const std::unordered_map<int, int>& allocation,
      const StackFrame& frame) const;

  // Calculate stack frame layout.
  StackFrame computeStackFrame(const ir::IRFunction& fn,
                               const regalloc::RegAllocResult& allocResult,
                               const LivenessResult& liveness);

  // Emit function prologue.
  void emitPrologue(const ir::IRFunction& fn, const StackFrame& frame,
                    std::vector<std::string>& out);

  // Emit function epilogue.
  void emitEpilogue(const ir::IRFunction& fn, const StackFrame& frame,
                    std::vector<std::string>& out);

  // Emit function body with register allocation.
  void emitFunctionBody(const ir::IRFunction& fn,
                        const std::unordered_map<int, int>& allocation,
                        const StackFrame& frame, const LivenessResult& liveness,
                        std::vector<std::string>& out);

  std::string binOpMnemonic(ir::BinaryOp op) const;
};

#endif  // BACKEND_CODEGEN_H
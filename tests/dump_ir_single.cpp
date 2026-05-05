#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "ast.h"
#include "ir.h"
#include "ir_generator.h"
#include "optimizer.h"
#include "parse_driver.h"
#include "semantic_analyzer.h"

using namespace ir;

extern CompUnit* root;

static std::string formatOperand(const Operand& op) {
  if (op.isImm()) return std::to_string(op.immValue);
  if (op.isVReg()) return "v" + std::to_string(op.vregId);
  return "@" + op.globalName;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: dump_ir_single <cfile>\n";
    return 1;
  }

  bool parsed = frontend::parse_from_file(argv[1]);
  if (!parsed || !root) {
    std::cerr << "parse failed\n";
    return 1;
  }

  SemanticAnalyzer sema;
  bool ok = sema.analyze(root);
  assert(ok);

  IRGenerator gen;
  IRProgram prog = gen.generate(root);
  OptimizeProgram(prog);

  for (const auto& fn : prog.functions) {
    if (fn.name != "func") continue;
    std::cout << "func params:";
    for (size_t i = 0; i < fn.params.size(); ++i) {
      std::cout << " [" << i << "]=v" << fn.params[i];
    }
    std::cout << "\n";

    for (size_t i = 0; i < fn.instructions.size(); ++i) {
      const auto& inst = fn.instructions[i];
      std::cout << i << ": ";
      switch (inst->kind) {
        case InstKind::Binary: {
          auto* b = static_cast<const BinaryInst*>(inst.get());
          std::cout << "v" << b->dest << " = bin(" << formatOperand(b->lhs)
                    << ", " << formatOperand(b->rhs) << ")";
          break;
        }
        case InstKind::Unary: {
          auto* u = static_cast<const UnaryInst*>(inst.get());
          std::cout << "v" << u->dest << " = unary(" << formatOperand(u->operand)
                    << ")";
          break;
        }
        case InstKind::Copy: {
          auto* c = static_cast<const CopyInst*>(inst.get());
          std::cout << "v" << c->dest << " = copy(" << formatOperand(c->src)
                    << ")";
          break;
        }
        case InstKind::Load: {
          auto* l = static_cast<const LoadInst*>(inst.get());
          std::cout << "v" << l->dest << " = load(" << formatOperand(l->addr)
                    << ")";
          break;
        }
        case InstKind::Store: {
          auto* s = static_cast<const StoreInst*>(inst.get());
          std::cout << "store(" << formatOperand(s->src) << ", "
                    << formatOperand(s->addr) << ")";
          break;
        }
        case InstKind::Branch: {
          auto* br = static_cast<const BranchInst*>(inst.get());
          std::cout << "branch(" << formatOperand(br->cond) << ")";
          break;
        }
        case InstKind::Jump: {
          auto* j = static_cast<const JumpInst*>(inst.get());
          std::cout << "jump(" << j->target << ")";
          break;
        }
        case InstKind::Call: {
          auto* c = static_cast<const CallInst*>(inst.get());
          if (c->hasDest) std::cout << "v" << c->dest << " = ";
          std::cout << "call " << c->callee << "(";
          for (size_t ai = 0; ai < c->args.size(); ++ai) {
            if (ai) std::cout << ", ";
            std::cout << formatOperand(c->args[ai]);
          }
          std::cout << ")";
          break;
        }
        case InstKind::Return: {
          auto* r = static_cast<const ReturnInst*>(inst.get());
          if (r->hasValue) std::cout << "return " << formatOperand(r->value);
          else std::cout << "return";
          break;
        }
        case InstKind::Label: {
          auto* l = static_cast<const LabelInst*>(inst.get());
          std::cout << "label " << l->label;
          break;
        }
      }
      std::cout << "\n";
    }
  }

  delete root;
  root = nullptr;
  return 0;
}

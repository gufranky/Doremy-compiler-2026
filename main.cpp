#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "ast.h"
#include "backend_codegen.h"
#include "ir_generator.h"
#include "optimizer.h"
#include "semantic_analyzer.h"

extern FILE* yyin;
extern int yyparse();
extern CompUnit* root;

int main(int argc, char** argv) {
  bool enableOpt = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "-opt") enableOpt = true;
  }

  // Read from stdin by default
  yyin = stdin;

  // Parse the input
  if (yyparse() != 0) {
    std::cerr << "Parsing failed!\n";
    return 1;
  }

  if (!root) {
    std::cerr << "No AST root generated!\n";
    return 1;
  }

  // Perform semantic analysis
  SemanticAnalyzer analyzer;
  if (!analyzer.analyze(root)) {
    std::cerr << "Semantic errors found:\n";
    for (const auto& error : analyzer.getErrors()) {
      std::cerr << "  Error: " << error << "\n";
    }
    delete root;
    return 1;
  }
  // IR generation
  IRGenerator generator;
  ir::IRProgram program = generator.generate(root);

  if (enableOpt) {
    ir::OptimizeProgram(program);
  }

  // Backend code generation (register allocation + assembly)
  CodeGen codegen;
  std::vector<std::string> asmLines = codegen.generate(program);

  // Output assembly to stdout
  for (const auto& line : asmLines) {
    std::cout << line << "\n";
  }

  // Clean up AST
  delete root;

  return 0;
}

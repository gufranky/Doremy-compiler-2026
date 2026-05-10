#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ast.h"
#include "backend_codegen.h"
#include "ir_generator.h"
#include "parse_driver.h"
#include "semantic_analyzer.h"

extern CompUnit* root;

namespace {

void printUsage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " -S -o <output.s> <input.sy> [-O1]"
            << " [--disable-midir|--midir-build-only|--midir-stop-after-ssa|--midir-lower-only]\n"
            << "Note: optimization and MidIR flags are accepted for compatibility only and currently have no effect.\n";
}

struct CommandLineOptions {
  bool emitAssembly = false;
  std::string inputFile;
  std::string outputFile;
};

bool parseCommandLine(int argc, char** argv, CommandLineOptions& options) {
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "-S") {
      options.emitAssembly = true;
    } else if (arg == "-O1" || arg == "--disable-midir" ||
               arg == "--midir-build-only" ||
               arg == "--midir-stop-after-ssa" ||
               arg == "--midir-lower-only") {
      // Legacy compatibility flags; optimization is currently disabled.
    } else if (arg == "-o") {
      if (i + 1 >= argc) {
        std::cerr << "Error: missing output file after -o\n";
        return false;
      }
      options.outputFile = argv[++i];
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Error: unsupported option " << arg << "\n";
      return false;
    } else {
      if (!options.inputFile.empty()) {
        std::cerr << "Error: multiple input files are not supported\n";
        return false;
      }
      options.inputFile = arg;
    }
  }

  if (!options.emitAssembly) {
    std::cerr << "Error: -S is required\n";
    return false;
  }
  if (options.outputFile.empty()) {
    std::cerr << "Error: -o <output.s> is required\n";
    return false;
  }
  if (options.inputFile.empty()) {
    std::cerr << "Error: input file is required\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  CommandLineOptions options;
  if (!parseCommandLine(argc, argv, options)) {
    printUsage(argv[0]);
    return 1;
  }

  // Parse the input
  bool parsed = frontend::parse_from_file(options.inputFile);
  if (!parsed) {
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

  // Backend code generation (register allocation + assembly)
  CodeGen codegen;
  std::vector<std::string> asmLines = codegen.generate(program);

  std::ofstream output(options.outputFile, std::ios::binary);
  if (!output) {
    std::cerr << "Error: Cannot open output file " << options.outputFile
              << "\n";
    delete root;
    return 1;
  }

  for (const auto& line : asmLines) {
    output << line << "\n";
  }
  if (!output) {
    std::cerr << "Error: Failed to write output file " << options.outputFile
              << "\n";
    delete root;
    return 1;
  }

  // Clean up AST
  delete root;

  return 0;
}

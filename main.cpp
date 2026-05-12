#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.h"
#include "backend_codegen.h"
#include "ir_generator.h"
#include "lower_midir.h"
#include "midir.h"
#include "midir_builder.h"
#include "optimizer_pipeline.h"
#include "parse_driver.h"
#include "semantic_analyzer.h"

extern CompUnit* root;

namespace {

void printUsage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " -S -o <output.s> <input.sy> [-O1]"
            << " [--disable-midir|--midir-build-only|--midir-stop-after-ssa|--midir-lower-only]\n";
}

struct CommandLineOptions {
  bool emitAssembly = false;
  bool enableMidIR = true;
  bool midirBuildOnly = false;
  bool stopAfterSSA = false;
  bool lowerOnly = false;
  std::string inputFile;
  std::string outputFile;
};

bool parseCommandLine(int argc, char** argv, CommandLineOptions& options) {
  bool sawOptimizationFlag = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "-S") {
      options.emitAssembly = true;
    } else if (arg == "-O1") {
      sawOptimizationFlag = true;
    } else if (arg == "--disable-midir") {
      options.enableMidIR = false;
    } else if (arg == "--midir-build-only") {
      options.midirBuildOnly = true;
    } else if (arg == "--midir-stop-after-ssa") {
      options.stopAfterSSA = true;
    } else if (arg == "--midir-lower-only") {
      options.lowerOnly = true;
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

  if (!sawOptimizationFlag) {
    options.enableMidIR = false;
    options.lowerOnly = false;
    options.stopAfterSSA = false;
    options.midirBuildOnly = false;
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

void removeUnusedFunctions(midir::Module& module) {
  if (module.functions.empty()) return;

  std::unordered_map<std::string, int> functionIndexByName;
  functionIndexByName.reserve(module.functions.size());
  for (int i = 0; i < static_cast<int>(module.functions.size()); ++i) {
    functionIndexByName[module.functions[i].name] = i;
  }

  auto entryIt = functionIndexByName.find("main");
  if (entryIt == functionIndexByName.end()) return;

  std::vector<char> reachable(module.functions.size(), 0);
  std::queue<int> worklist;
  reachable[entryIt->second] = 1;
  worklist.push(entryIt->second);

  while (!worklist.empty()) {
    const int functionIndex = worklist.front();
    worklist.pop();
    const auto& function = module.functions[functionIndex];
    for (const auto& block : function.blocks) {
      for (const auto& inst : block.instructions) {
        if (inst.kind != midir::InstKind::Call) continue;
        auto calleeIt = functionIndexByName.find(inst.callee);
        if (calleeIt == functionIndexByName.end()) continue;
        if (reachable[calleeIt->second]) continue;
        reachable[calleeIt->second] = 1;
        worklist.push(calleeIt->second);
      }
    }
  }

  std::vector<midir::Function> keptFunctions;
  keptFunctions.reserve(module.functions.size());
  for (int i = 0; i < static_cast<int>(module.functions.size()); ++i) {
    if (reachable[i]) keptFunctions.push_back(std::move(module.functions[i]));
  }
  module.functions = std::move(keptFunctions);
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
  ir::IRProgram bridgeProgram;

  if (options.enableMidIR) {
    midir::MidIRBuilder midirBuilder;
    midir::Module midirModule = midirBuilder.build(root);

    if (options.midirBuildOnly) {
      std::ofstream output(options.outputFile, std::ios::binary);
      if (!output) {
        std::cerr << "Error: Cannot open output file " << options.outputFile
                  << "\n";
        delete root;
        return 1;
      }
      output << midir::dumpModule(midirModule);
      delete root;
      return 0;
    }

    if (!options.lowerOnly) {
      midir::PassManager passManager;
      passManager.addFunctionPass(std::make_unique<midir::VerifySSAPass>());
      passManager.addFunctionPass(std::make_unique<midir::InlinePass>(&midirModule));
      passManager.addFunctionPass(std::make_unique<midir::VerifySSAPass>());
      passManager.addFunctionPass(std::make_unique<midir::SimplifyCFGPass>());
      passManager.addFunctionPass(std::make_unique<midir::LoopSimplifyPass>());
      passManager.addFunctionPass(std::make_unique<midir::LoopRotatePass>());
      passManager.addFunctionPass(std::make_unique<midir::LoopSimplifyPass>());
      passManager.addFunctionPass(std::make_unique<midir::LCSSAPass>());
      passManager.addFunctionPass(std::make_unique<midir::VerifySSAPass>());
      passManager.addFunctionPass(std::make_unique<midir::LICMPass>());
      passManager.addFunctionPass(std::make_unique<midir::VerifySSAPass>());
      passManager.addFunctionPass(std::make_unique<midir::IndVarSimplifyPass>());
      passManager.addFunctionPass(std::make_unique<midir::SimpleLoopUnrollPass>());
      passManager.addFunctionPass(std::make_unique<midir::InstCombinePass>());
      passManager.addFunctionPass(std::make_unique<midir::ConstDivRemPass>(&midirModule));
      passManager.addFunctionPass(std::make_unique<midir::GVNPass>());
      passManager.addFunctionPass(std::make_unique<midir::EarlyCSEPass>());
      passManager.addFunctionPass(std::make_unique<midir::DSEPass>());
      passManager.addFunctionPass(std::make_unique<midir::ADCEPass>());
      passManager.addFunctionPass(std::make_unique<midir::SimplifyCFGPass>());
      passManager.addFunctionPass(std::make_unique<midir::InstCombinePass>());
      passManager.addFunctionPass(std::make_unique<midir::GVNPass>());
      passManager.addFunctionPass(std::make_unique<midir::EarlyCSEPass>());
      passManager.addFunctionPass(std::make_unique<midir::DSEPass>());
      passManager.addFunctionPass(std::make_unique<midir::ADCEPass>());
      passManager.addFunctionPass(std::make_unique<midir::SimplifyCFGPass>());
      passManager.addFunctionPass(std::make_unique<midir::InstCombinePass>());
      passManager.addFunctionPass(std::make_unique<midir::VerifySSAPass>());
      try {
        passManager.run(midirModule);
      } catch (const std::exception& ex) {
        std::cerr << "MidIR verification failed: " << ex.what() << "\n";
        delete root;
        return 1;
      }
      removeUnusedFunctions(midirModule);
    }

    if (options.stopAfterSSA) {
      std::ofstream output(options.outputFile, std::ios::binary);
      if (!output) {
        std::cerr << "Error: Cannot open output file " << options.outputFile
                  << "\n";
        delete root;
        return 1;
      }
      output << midir::dumpModule(midirModule);
      delete root;
      return 0;
    }

    midir::LowerMidIR lowerMidIR;
    bridgeProgram = lowerMidIR.lower(midirModule);
  } else {
    IRGenerator generator;
    bridgeProgram = generator.generate(root);
  }

  // Backend code generation (register allocation + assembly)
  CodeGen codegen;
  std::vector<std::string> asmLines = codegen.generate(bridgeProgram);

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

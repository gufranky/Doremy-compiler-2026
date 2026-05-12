#ifndef OPTIMIZER_PIPELINE_H
#define OPTIMIZER_PIPELINE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "analysis.h"

namespace midir {

struct PassResult {
  bool changed = false;
};

class AnalysisManager {
 public:
  const DominatorTree& getDominatorTree(Function& function);
  const LoopInfo& getLoopInfo(Function& function);
  void invalidate(Function& function);
  void invalidateAll();

 private:
  std::unordered_map<Function*, DominatorTree> dominator_trees_;
  std::unordered_map<Function*, LoopInfo> loop_infos_;
};

class FunctionPass {
 public:
  virtual ~FunctionPass() = default;
  virtual std::string name() const = 0;
  virtual PassResult run(Function& function, AnalysisManager& analysisManager) = 0;
};

class VerifySSAPass : public FunctionPass {
 public:
  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;
};

class InlinePass : public FunctionPass {
 public:
  explicit InlinePass(Module* module) : module_(module) {}

  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;

 private:
  Module* module_ = nullptr;
};

class SimplifyCFGPass : public FunctionPass {
 public:
  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;
};

class LoopSimplifyPass : public FunctionPass {
 public:
  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;
};

class LoopRotatePass : public FunctionPass {
 public:
  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;
};

class SimpleLoopUnrollPass : public FunctionPass {
 public:
  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;
};

class LCSSAPass : public FunctionPass {
 public:
  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;
};

class LICMPass : public FunctionPass {
 public:
  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;
};

class IndVarSimplifyPass : public FunctionPass {
 public:
  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;
};

class InstCombinePass : public FunctionPass {
 public:
  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;
};

class ConstDivRemPass : public FunctionPass {
 public:
  explicit ConstDivRemPass(Module* module) : module_(module) {}

  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;

 private:
  Module* module_ = nullptr;
};

class EarlyCSEPass : public FunctionPass {
 public:
  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;
};

class ADCEPass : public FunctionPass {
 public:
  std::string name() const override;
  PassResult run(Function& function, AnalysisManager& analysisManager) override;
};

class PassManager {
 public:
  void addFunctionPass(std::unique_ptr<FunctionPass> pass);
  bool run(Module& module);

 private:
  AnalysisManager analysis_manager_;
  std::vector<std::unique_ptr<FunctionPass>> function_passes_;
};

}  // namespace midir

#endif

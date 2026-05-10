#include "optimizer_pipeline.h"

#include <utility>

namespace midir {

const DominatorTree& AnalysisManager::getDominatorTree(Function& function) {
  auto it = dominator_trees_.find(&function);
  if (it != dominator_trees_.end()) {
    return it->second;
  }
  auto [inserted, _] =
      dominator_trees_.emplace(&function, buildDominatorTree(function));
  return inserted->second;
}

const LoopInfo& AnalysisManager::getLoopInfo(Function& function) {
  auto it = loop_infos_.find(&function);
  if (it != loop_infos_.end()) {
    return it->second;
  }
  const DominatorTree& dom_tree = getDominatorTree(function);
  auto [inserted, _] =
      loop_infos_.emplace(&function, buildLoopInfo(function, dom_tree));
  return inserted->second;
}

void AnalysisManager::invalidate(Function& function) {
  dominator_trees_.erase(&function);
  loop_infos_.erase(&function);
}

void AnalysisManager::invalidateAll() {
  dominator_trees_.clear();
  loop_infos_.clear();
}

void PassManager::addFunctionPass(std::unique_ptr<FunctionPass> pass) {
  function_passes_.push_back(std::move(pass));
}

bool PassManager::run(Module& module) {
  bool changed = false;
  for (auto& function : module.functions) {
    for (const auto& pass : function_passes_) {
      PassResult result = pass->run(function, analysis_manager_);
      changed = changed || result.changed;
      if (result.changed) {
        analysis_manager_.invalidate(function);
      }
    }
  }
  return changed;
}

}  // namespace midir

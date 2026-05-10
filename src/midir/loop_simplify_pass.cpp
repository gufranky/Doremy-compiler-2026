#include "optimizer_pipeline.h"
#include "optimizer_utils.h"

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

namespace midir {

namespace {

bool loopContainsBlock(const Loop& loop, int block) {
  return std::binary_search(loop.blocks.begin(), loop.blocks.end(), block);
}

bool valueRefsEqual(const ValueRef& lhs, const ValueRef& rhs) {
  return lhs.kind == rhs.kind && lhs.type == rhs.type &&
         lhs.value_id == rhs.value_id && lhs.int_value == rhs.int_value &&
         lhs.float_value == rhs.float_value &&
         lhs.frame_offset == rhs.frame_offset && lhs.symbol == rhs.symbol;
}

std::string makeUniqueBlockName(const Function& function, const std::string& base) {
  if (function.block_index_by_name.count(base) == 0) return base;
  for (int suffix = 1;; ++suffix) {
    std::string candidate = base + "." + std::to_string(suffix);
    if (function.block_index_by_name.count(candidate) == 0) return candidate;
  }
}

ValueRef buildPreheaderIncomingValue(Function& function,
                                     const std::vector<PhiIncoming>& incomings,
                                     std::vector<Instruction>& preheaderInstructions) {
  if (incomings.empty()) return ValueRef::Invalid();
  const ValueRef& first = incomings.front().value;
  if (std::all_of(incomings.begin(), incomings.end(), [&](const PhiIncoming& incoming) {
        return valueRefsEqual(incoming.value, first);
      })) {
    return first;
  }

  Instruction phi;
  phi.kind = InstKind::Phi;
  phi.result_type = first.type;
  phi.result_id = function.newValue(first.type);
  phi.has_result = true;
  phi.incomings = incomings;
  preheaderInstructions.push_back(std::move(phi));
  return ValueRef::SSA(preheaderInstructions.back().result_id,
                       preheaderInstructions.back().result_type);
}

bool ensurePreheader(Function& function, const Loop& loop) {
  if (loop.header < 0 || loop.header >= static_cast<int>(function.blocks.size())) {
    return false;
  }
  if (getLoopPreheader(function, loop) >= 0) return false;

  std::vector<int> outside_preds;
  for (int pred : function.blocks[loop.header].preds) {
    if (!loopContainsBlock(loop, pred) &&
        std::find(outside_preds.begin(), outside_preds.end(), pred) ==
            outside_preds.end()) {
      outside_preds.push_back(pred);
    }
  }
  if (outside_preds.empty()) return false;

  const int new_index = static_cast<int>(function.blocks.size());
  BasicBlock preheader;
  preheader.name =
      makeUniqueBlockName(function, function.blocks[loop.header].name + ".preheader");

  auto& header = function.blocks[loop.header];
  for (auto& inst : header.instructions) {
    if (inst.kind != InstKind::Phi) break;

    std::vector<PhiIncoming> inside_incomings;
    std::vector<PhiIncoming> outside_incomings;
    inside_incomings.reserve(inst.incomings.size());
    outside_incomings.reserve(inst.incomings.size());
    for (const auto& incoming : inst.incomings) {
      if (loopContainsBlock(loop, incoming.pred_block)) {
        inside_incomings.push_back(incoming);
      } else {
        outside_incomings.push_back(incoming);
      }
    }
    if (outside_incomings.empty()) continue;

    ValueRef merged =
        buildPreheaderIncomingValue(function, outside_incomings, preheader.instructions);
    inside_incomings.push_back(PhiIncoming{new_index, merged});
    inst.incomings = std::move(inside_incomings);
  }

  Instruction jump;
  jump.kind = InstKind::Jump;
  jump.jump_target = header.name;
  preheader.instructions.push_back(std::move(jump));

  function.blocks.push_back(std::move(preheader));
  function.block_index_by_name[function.blocks.back().name] = new_index;

  for (int pred : outside_preds) {
    redirectPredecessorTerminator(function, pred, loop.header, new_index);
  }

  rebuildEdges(function);
  normalizePhiIncomings(function);
  return true;
}

}  // namespace

std::string LoopSimplifyPass::name() const { return "loop-simplify"; }

PassResult LoopSimplifyPass::run(Function& function,
                                 AnalysisManager& analysisManager) {
  rebuildEdges(function);
  const LoopInfo& loop_info = analysisManager.getLoopInfo(function);

  std::vector<int> order(loop_info.loops.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
    return loop_info.loops[lhs].depth > loop_info.loops[rhs].depth;
  });

  bool changed = false;
  for (int loop_index : order) {
    changed = ensurePreheader(function, loop_info.loops[loop_index]) || changed;
  }

  if (changed) {
    rebuildEdges(function);
    normalizePhiIncomings(function);
    analysisManager.invalidate(function);
  }
  return PassResult{changed};
}

}  // namespace midir

#ifndef OPTIMIZER_UTILS_H
#define OPTIMIZER_UTILS_H

#include <string>
#include <unordered_map>
#include <vector>

#include "analysis.h"

namespace midir {

bool isTerminator(InstKind kind);
bool isPointerType(Type type);
bool isIntegerLike(Type type);
bool isNumericScalar(Type type);
bool isValidCopyTypes(Type dst, Type src);
bool isCommutativeBinaryOp(ir::BinaryOp op);
bool hasSideEffects(const Instruction& inst);
bool isPureComputingInstruction(const Instruction& inst);
bool isTrackableLocation(const MemoryLocation& location);
std::string valueIdentityKey(const ValueRef& value);
std::string aliasClassKey(const MemoryLocation& location);
std::vector<std::string> affectedAliasClassKeys(const MemoryLocation& location);
int currentAliasVersion(const std::unordered_map<std::string, int>& aliasVersions,
                        const MemoryLocation& location);
void bumpAliasVersions(std::unordered_map<std::string, int>& aliasVersions,
                       const MemoryLocation& location);
std::string memoryAccessKey(const ValueRef& addr, const MemoryLocation& location,
                            Type accessType);

void validateValueRefShape(const Function& function, const ValueRef& value,
                           const std::string& context);
void rebuildEdges(Function& function);
bool redirectPredecessorTerminator(Function& function, int pred_block,
                                   int old_succ_block, int new_succ_block);
void rewritePhiForEdgeRedirect(Function& function, int succ_block,
                               int old_pred_block,
                               const std::vector<int>& new_pred_blocks);
void normalizePhiIncomings(Function& function);
bool removeBlocksAndRemap(Function& function, const std::vector<bool>& removed);
void pruneUnreachableBlocks(Function& function);
Instruction makeCopyInstruction(const Instruction& original,
                                const ValueRef& replacement);
std::vector<int> buildUseCounts(const Function& function);
ValueRef resolveReplacement(ValueRef value,
                            const std::vector<ValueRef>& replacements);
void rewriteInstructionOperands(Instruction& inst,
                                const std::vector<ValueRef>& replacements);
void replaceAllUses(Function& function, int valueId, const ValueRef& replacement);

}  // namespace midir

#endif

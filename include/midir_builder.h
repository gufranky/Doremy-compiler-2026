#ifndef MIDIR_BUILDER_H
#define MIDIR_BUILDER_H

#include "ast.h"
#include "midir.h"

namespace midir {

class MidIRBuilder {
 public:
  Module build(const ir::IRProgram& bridgeProgram) const;
  Module build(CompUnit* root) const;
};

}  // namespace midir

#endif

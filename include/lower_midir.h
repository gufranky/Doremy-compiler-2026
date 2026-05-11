#ifndef LOWER_MIDIR_H
#define LOWER_MIDIR_H

#include "midir.h"

namespace midir {

class LowerMidIR {
 public:
  ir::IRProgram lower(const Module& module) const;
};

}  // namespace midir

#endif

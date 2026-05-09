#include "midir.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_set>

namespace midir {

#include "midir_impl_core.inc"
#include "midir_impl_build_lower.inc"
#include "midir_impl_verify_analysis.inc"
#include "midir_impl_ssa.inc"
#include "midir_impl_passes.inc"

}  // namespace midir

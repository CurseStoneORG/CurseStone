#include "DefaultPermissions.h"
namespace mcppeserver::permission {
void DefaultPermissions::registerCorePermissions(Permissible& p) {
    // Sub-tree "mcppeserver.*" — given to ops only by default.
    (void)p;
    // Real impl uses setPermission on the Permissible.
}
} // namespace

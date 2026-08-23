#include "DefaultPermissions.h"
namespace cenisyspe::permission {
void DefaultPermissions::registerCorePermissions(Permissible& p) {
    // Sub-tree "cenisyspe.*" — given to ops only by default.
    (void)p;
    // Real impl uses setPermission on the Permissible.
}
} // namespace

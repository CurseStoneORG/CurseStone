#include "DefaultPermissions.h"
namespace atomikpe::permission {
void DefaultPermissions::registerCorePermissions(Permissible& p) {
    // Sub-tree "atomikpe.*" — given to ops only by default.
    (void)p;
    // Real impl uses setPermission on the Permissible.
}
} // namespace

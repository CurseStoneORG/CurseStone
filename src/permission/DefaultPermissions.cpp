#include "DefaultPermissions.h"
namespace cursestone::permission {
void DefaultPermissions::registerCorePermissions(Permissible& p) {
    // Sub-tree "cursestone.*" — given to ops only by default.
    (void)p;
    // Real impl uses setPermission on the Permissible.
}
} // namespace

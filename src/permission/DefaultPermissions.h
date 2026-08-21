// DefaultPermissions — registers the standard permission tree used
// by CommandSender::hasPermission. Mirrors pocketmine/permission/
// DefaultPermissions.
#pragma once
#include "Permissible.h"
namespace mcppeserver::permission {
class DefaultPermissions {
public:
    static void registerCorePermissions(Permissible& p);
};
} // namespace

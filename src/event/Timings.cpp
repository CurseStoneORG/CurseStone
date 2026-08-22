#include "Timings.h"
namespace mcppeserver::event {
Timings& Timings::get() { static Timings t; return t; }
} // namespace

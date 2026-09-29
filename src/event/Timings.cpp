#include "Timings.h"
namespace cursestone::event {
Timings& Timings::get() { static Timings t; return t; }
} // namespace

#include "Timings.h"
namespace cenisyspe::event {
Timings& Timings::get() { static Timings t; return t; }
} // namespace

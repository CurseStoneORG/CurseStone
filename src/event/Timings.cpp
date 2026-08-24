#include "Timings.h"
namespace atomikpe::event {
Timings& Timings::get() { static Timings t; return t; }
} // namespace

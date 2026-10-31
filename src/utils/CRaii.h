// SPDX-License-Identifier: LGPL-3.0-or-later
// CRaii.h — RAII handles for raw memory the serialize helpers still hand
// out and for NBT roots. Owning C++ code holds these instead of raw
// pointers so early returns cannot leak.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <memory>

#include "nbt/tag/Tag.h"

namespace cursestone::utils {

// Byte buffer returned by helpers that allocate with malloc (caller frees).
struct FreeDeleter {
    void operator()(void* p) const noexcept { free(p); }
};
using BufferPtr = std::unique_ptr<uint8_t[], FreeDeleter>;

// NBT tag owned by a single C++ scope; destroyed (with its children)
// when the guard leaves scope.
using TagGuard = std::unique_ptr<cursestone::nbt::Tag>;

}  // namespace cursestone::utils

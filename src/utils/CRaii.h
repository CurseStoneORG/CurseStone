// SPDX-License-Identifier: LGPL-3.0-or-later
// CRaii.h — RAII handles for memory the C core allocates. Owning C++ code
// holds these instead of raw pointers so early returns cannot leak.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <memory>

#include "nbt/tag/Tag.h"

namespace cursestone::utils {

// Byte buffer returned by the C serialize/write helpers (caller must free).
struct FreeDeleter {
    void operator()(void* p) const noexcept { free(p); }
};
using BufferPtr = std::unique_ptr<uint8_t[], FreeDeleter>;

// NBT tag owned by a single C++ scope; shared roots keep using NbtPtr.
struct TagDeleter {
    void operator()(mcp_tag_t* t) const noexcept {
        if (t)
            mcp_tag_destroy(t);
    }
};
using TagGuard = std::unique_ptr<mcp_tag_t, TagDeleter>;

}  // namespace cursestone::utils

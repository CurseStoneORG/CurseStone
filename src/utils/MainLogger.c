// SPDX-License-Identifier: LGPL-3.0-or-later
// MainLogger.c — delegates to the async logger (src/logger/AsyncMainLogger).
//
// This file preserves backward compatibility:
//   - `mcp_main_logger_init()` → calls `mcp_main_logger_async_init()`
//   - `mcp_main_logger_get_instance()` → returns async logger (cast)
//   - `mcp_main_logger_destroy()` → calls `mcp_main_logger_async_destroy()`
//
// The async logger struct (`mcp_async_logger_t`) has `mcp_logger_t vtable`
// as its first field, making it layout-compatible with `mcp_main_logger_t`.
// The C++ wrapper `utils::MainLogger::get()` accesses `->vtable` which
// works correctly regardless of which struct is behind the pointer.
#include "MainLogger.h"
#include "../logger/AsyncMainLogger.h"

void mcp_main_logger_init(const char* log_path, bool colored) {
    mcp_main_logger_async_init(log_path, colored);
}

mcp_main_logger_t* mcp_main_logger_get_instance(void) {
    if (!mcp_async_logger_is_initialized()) {
        mcp_main_logger_async_init(NULL, true);
    }
    // The async logger has `mcp_logger_t vtable` as its first field,
    // so casting its address to `mcp_main_logger_t*` is safe — callers
    // only access the vtable.
    return (mcp_main_logger_t*)mcp_async_logger_get_instance();
}

void mcp_main_logger_destroy(void) {
    mcp_main_logger_async_destroy();
}

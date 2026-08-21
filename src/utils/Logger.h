// SPDX-License-Identifier: LGPL-3.0-or-later
// Logger — abstract logger interface. Pure C function-pointer vtable.
// Concrete implementations: MainLogger (stdout+file), NullLogger.
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mcp_log_level {
    MCP_LOG_DEBUG = 0,
    MCP_LOG_INFO  = 1,
    MCP_LOG_NOTICE= 2,
    MCP_LOG_WARN  = 3,
    MCP_LOG_ERROR = 4,
    MCP_LOG_CRITICAL = 5,
    MCP_LOG_EMERGENCY = 6
} mcp_log_level_t;

typedef struct mcp_logger {
    void (*log)(void* self, mcp_log_level_t lvl, const char* msg);
    void (*debug)(void* self, const char* msg);
    void (*info)(void* self, const char* msg);
    void (*notice)(void* self, const char* msg);
    void (*warn)(void* self, const char* msg);
    void (*error)(void* self, const char* msg);
    void (*critical)(void* self, const char* msg);
    void (*emergency)(void* self, const char* msg);
    void (*log_exception)(void* self, const char* what, const char* where);
    void (*shutdown)(void* self);
    void* impl;
} mcp_logger_t;

// Helper macros for use through the vtable.
#define MCP_LOG(lg, lvl, msg)   ((lg) ? (lg)->log((lg)->impl, (lvl), (msg)) : (void)0)
#define MCP_LOG_DEBUG(lg, msg)  ((lg) ? (lg)->debug((lg)->impl, (msg)) : (void)0)
#define MCP_LOG_INFO(lg, msg)   ((lg) ? (lg)->info((lg)->impl, (msg)) : (void)0)
#define MCP_LOG_NOTICE(lg, msg) ((lg) ? (lg)->notice((lg)->impl, (msg)) : (void)0)
#define MCP_LOG_WARN(lg, msg)   ((lg) ? (lg)->warn((lg)->impl, (msg)) : (void)0)
#define MCP_LOG_ERROR(lg, msg)  ((lg) ? (lg)->error((lg)->impl, (msg)) : (void)0)
#define MCP_LOG_CRIT(lg, msg)   ((lg) ? (lg)->critical((lg)->impl, (msg)) : (void)0)
#define MCP_LOG_EMERG(lg, msg)  ((lg) ? (lg)->emergency((lg)->impl, (msg)) : (void)0)

const char* mcp_log_level_name(mcp_log_level_t lvl);

#ifdef __cplusplus
} // extern "C"
#endif

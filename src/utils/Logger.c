// SPDX-License-Identifier: LGPL-3.0-or-later
// Logger.c — common helpers shared by concrete logger implementations.
#include "Logger.h"

const char* mcp_log_level_name(mcp_log_level_t lvl) {
    switch (lvl) {
        case MCP_LOG_DEBUG:     return "DEBUG";
        case MCP_LOG_INFO:      return "INFO";
        case MCP_LOG_NOTICE:   return "NOTICE";
        case MCP_LOG_WARN:      return "WARNING";
        case MCP_LOG_ERROR:     return "ERROR";
        case MCP_LOG_CRITICAL:  return "CRITICAL";
        case MCP_LOG_EMERGENCY: return "EMERGENCY";
    }
    return "UNKNOWN";
}

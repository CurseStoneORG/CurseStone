// SPDX-License-Identifier: LGPL-3.0-or-later
// RakLibServer — the RakLib worker thread entrypoint. Pure C.
// In a C++ build this is wrapped by a thread subclass; for now we
// expose the tick loop directly so it can run on the main thread too.
#pragma once

#include "SessionManager.h"
#include "ServerHandler.h"
#include "ServerInstance.h"
#include "UDPServerSocket.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_raklib_server {
    mcp_rak_session_manager_t manager;
    mcp_server_handler_t     handler;
    mcp_server_instance_t    instance;
    int64_t                  server_id;
    bool                     running;
} mcp_raklib_server_t;

// Initialize (does not bind). Returns 0 on success.
int mcp_raklib_server_init(mcp_raklib_server_t* s, int64_t server_id,
                              uint16_t mtu, void* instance_impl);

// Bind and start the loop. Returns 0 on success.
int mcp_raklib_server_start(mcp_raklib_server_t* s, const char* host,
                              uint16_t port);

// Tick once (call from main thread if not running worker).
void mcp_raklib_server_tick(mcp_raklib_server_t* s);

// Shutdown.
void mcp_raklib_server_stop(mcp_raklib_server_t* s);

#ifdef __cplusplus
} // extern "C"
#endif

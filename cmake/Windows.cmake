# cmake/Windows.cmake — Windows (MSVC + MinGW) build config
# This file is included only when WIN32 is true.

if(MSVC)
    # MSVC: use UTF-8 source / execution charset, suppress secure-CRT noise
    add_compile_options(/utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS)
    # Winsock2 needs to come before windows.h — we add WIN32_LEAN_AND_MEAN globally
    target_compile_definitions(MCPPEServer PRIVATE WIN32_LEAN_AND_MEAN _WINSOCKAPI_=0)
else()
    # MinGW: enable posix threads, win32 mutexes are flaky
    add_compile_options(-mthreads)
endif()

# Link winsock2 + ws2_32 (RakLib UDP socket)
target_link_libraries(MCPPEServer PRIVATE ws2_32 wsock32)

message(STATUS "Windows build: ${CMAKE_CXX_COMPILER_ID}")

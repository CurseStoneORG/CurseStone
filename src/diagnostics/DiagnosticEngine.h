/*
 _____                     _____ _
/  __ \                   /  ___| |
| | /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// DiagnosticEngine.h — the CurseStone Diagnostic Engine.
//
// The console presentation layer for everything that goes wrong (and
// everything that loads) inside CurseStone. Inspired by the error output of
// modern compilers (GCC, Clang, Rust): a calm, minimal console while things
// are fine, and a precise, compiler-style report the moment they are not —
// what failed, where, why, and how to fix it.
//
//   [17:14:40] ERROR Failed to load plugin 'ExamplePlugin2'
//
//     --> plugins/ExamplePlugin2/plugin.yml:4
//
//     error[E1002]: incompatible plugin API version
//
//      |
//    4 | api: 0.8.0
//      |      ^^^^^ expected API version 1.0.3
//      |
//
//     help: This plugin requires an older API version.
//     help: Update the plugin to support API 1.0.3.
//
// Diagnostic codes are grouped by category:
//   E0000  Configuration
//   E1000  Plugins
//   E2000  Worlds
//   E3000  Network
//   E4000  Blocks and Items
//   E5000  Internal Server Errors
// See docs/DIAGNOSTIC_ENGINE.md for the full table.
//
// Honesty rule: locations and snippets are rendered ONLY when the call site
// provides a real file and line (helpers below scan the actual file text).
// Nothing is ever invented — when there is no location, the block renders
// without the "-->" and the snippet, never with a guessed one.
//
// Blocks are pushed through the async logger's raw passthrough, carrying
// Minecraft § color codes: the console converts them to ANSI, the log file
// strips them. Startup failures that cannot be recovered abort the boot
// AFTER their diagnostic is printed (never silently).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cursestone {
namespace diagnostics {

enum class DiagSeverity {
    Note,  // informational context attached to an issue
    Warn,  // recoverable problem — counted in the startup summary
    Error, // the operation failed — counted in the startup summary
    Fatal  // startup cannot continue
};

// One compiler-style diagnostic. `file`, `line`, `col` and `span` are only
// set when they are KNOWN (scan the real file — see the helpers at the end
// of this header). 0 means "unknown" and the corresponding piece is omitted.
struct Diagnostic {
    int code = 0; // E<code>; 0 renders a plain block
    DiagSeverity severity = DiagSeverity::Error;
    std::string title;  // one-line summary for the tagged header line
    std::string file;   // display path as the operator knows it
    int line = 0;       // 1-based source line
    int col = 0;        // 1-based column of the highlighted value
    int span = 0;       // caret width; 0 = to the end of the (trimmed) line
    std::string label;  // text printed after the carets
    std::vector<std::string> notes; // "note:" lines (context, no blame)
    std::vector<std::string> help;  // "help:" lines (how to fix it)
};

class DiagEngine {
  public:
    // Identity banner, printed once from main() before the logger exists:
    //   CurseStone v1.0.3 "Obsidian"
    //   Minecraft PE 0.15.10 | Protocol 84
    static void banner();

    // ---- startup stages ----
    static void stage(const std::string& text);      // "Loading worlds..."
    static void stageItem(const std::string& text);  // "+ ExampleWorld loaded!"
    static void stageDone(const std::string& text);  // "Worlds loaded!"

    // Boot epilogue: "Server ready! (41ms)" and, when recoverable issues
    // were emitted during startup, the summary line.
    static void ready(unsigned long boot_ms);

    // ---- diagnostics ----
    // Render a diagnostic. Returns true when it counted as an issue
    // (severity Warn or Error).
    static bool emit(const Diagnostic& d);

    // Fatal variant: emit, then "Startup aborted." The caller must make the
    // server stop booting afterwards.
    static void fatal(const Diagnostic& d);

    // Manual issue accounting for warnings that are logged through the
    // plain logger (emit() already counts its own).
    static void noteIssue();
    static size_t issues();

    // Canonical description + default help for a diagnostic code (NULL when
    // the code is unknown). Call sites may append more specific help lines.
    static const char* codeDescription(int code);
    static const char* codeHelp(int code);

    // ---- shared file-scanning helpers (real locations only) ----
    // Locate a key in a text file's content. For INI ("key=value") and YAML
    // ("key: value") alike, reports the 1-based line of the FIRST match and
    // the 1-based column + span of the value. Returns false when the key is
    // not present (then no location is rendered).
    static bool findKeySpan(const std::string& text, const std::string& key, bool ini, int* line,
                            int* col, int* span);

    // Extract the first "...<entry>:<line>:" line number from a Lua error
    // message ("<entry>" as emitted by Lua, e.g. "src/main.lua"). Returns 0
    // when the message does not name one.
    static int luaErrorLine(const std::string& err, const std::string& entry);
};

} // namespace diagnostics
} // namespace cursestone

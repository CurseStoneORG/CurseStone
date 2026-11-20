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
 * You may study, modify, and redistribute it under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// DiagnosticEngine.cpp — see DiagnosticEngine.h for the design contract.

#include "DiagnosticEngine.h"

#include "../CurseStone.h"
#include "../utils/MainLogger.hpp"
#include "../utils/Terminal.h"
#include "../utils/TextFormat.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace cursestone {
namespace diagnostics {

namespace {

std::atomic<size_t> g_issues{0};

// UTF-8 Minecraft § (0xC2 0xA7) + code char, the encoding the whole codebase
// uses for colorable console text.
std::string sec(char code) {
    std::string s = "\xc2\xa7";
    s.push_back(code);
    return s;
}

// Minimalist palette, expressed through the shared § -> ANSI table so the
// console, log file and every converter agree on one map.
//   §8 dim gray (structure: pipes, gutters, timestamps)
//   §9 soft blue (locations)
//   §c soft red (errors, carets)
//   §e soft amber (warnings)
//   §b soft cyan (help)
//   §7 gray (secondary text)
//   §f default (primary text)
constexpr char kDim = '8';
constexpr char kLocation = '9';
constexpr char kErrorColor = 'c';
constexpr char kWarnColor = 'e';
constexpr char kHelp = 'b';
constexpr char kSecondary = '7';
constexpr char kPrimary = 'f';

const char* severityTag(DiagSeverity s) {
    switch (s) {
    case DiagSeverity::Note:
        return "INFO ";
    case DiagSeverity::Warn:
        return "WARN ";
    case DiagSeverity::Error:
        return "ERROR";
    case DiagSeverity::Fatal:
        return "FATAL";
    }
    return "ERROR";
}

mcp_log_level_t severityLevel(DiagSeverity s) {
    switch (s) {
    case DiagSeverity::Note:
        return MCP_LOG_INFO;
    case DiagSeverity::Warn:
        return MCP_LOG_WARN;
    case DiagSeverity::Error:
        return MCP_LOG_ERROR;
    case DiagSeverity::Fatal:
        return MCP_LOG_CRITICAL;
    }
    return MCP_LOG_ERROR;
}

std::string severityColor(DiagSeverity s) {
    return sec(s == DiagSeverity::Warn ? kWarnColor : kErrorColor);
}

// Read the 1-based `line` from `path` into `out` (trailing whitespace and CR
// trimmed, display-capped). Returns false when the file or the line does not
// exist. Early-out at the target line: boot diagnostics are rare, so a plain
// streaming scan beats any cache.
bool readSnippetLine(const std::string& path, int line, std::string& out) {
    if (line <= 0)
        return false;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::string row;
    int current = 0;
    while (current < line && std::getline(in, row)) {
        ++current;
    }
    if (current != line)
        return false;
    if (!row.empty() && row.back() == '\r')
        row.pop_back();
    while (!row.empty() && (row.back() == ' ' || row.back() == '\t'))
        row.pop_back();
    if (row.size() > 120)
        row = row.substr(0, 120) + " ...";
    out = row;
    return true;
}

// E-codes render zero-padded to four digits: E0003, E1002, E5002.
std::string codeStr(int code) {
    std::string n = std::to_string(code);
    while (n.size() < 4)
        n.insert(n.begin(), '0');
    return n;
}

// Right-aligned line number gutter (rustc style): " 4 | ".
std::string gutterNum(int line, int width) {
    std::string n = std::to_string(line);
    while ((int)n.size() < width)
        n.insert(n.begin(), ' ');
    return n;
}

int digitsOf(int v) {
    int d = 1;
    while (v >= 10) {
        v /= 10;
        ++d;
    }
    return d;
}

// The diagnostic code registry — one row per code: description + default
// help. Categories: E0000 configuration, E1000 plugins, E2000 worlds,
// E3000 network, E4000 blocks/items, E5000 internal.
struct DiagCodeInfo {
    int code;
    const char* description;
    const char* help;
};

constexpr DiagCodeInfo kDiagCodes[] = {
    // E0000 — Configuration.
    {1, "configuration file cannot be read",
     "Check the file's permissions and ownership, then retry."},
    {2, "configuration file is corrupt",
     "Fix the syntax error or delete the file to regenerate defaults."},
    {3, "configuration value is invalid",
     "Correct the value; the built-in default is used until then."},
    {4, "required configuration key is missing",
     "Add the key, or delete the file to regenerate defaults."},
    // E1000 — Plugins.
    {1001, "plugin manifest (plugin.yml) is missing or unreadable",
     "Create a plugin.yml with api/name/author/main fields."},
    {1002, "incompatible plugin API version", nullptr},
    {1003, "invalid plugin manifest field", nullptr},
    {1004, "plugin entry script is missing",
     "Point 'main' at an existing script inside src/."},
    {1005, "plugin script failed to parse", nullptr},
    {1006, "plugin failed during load", nullptr},
    {1007, "duplicate plugin identity",
     "Keep only one copy of the plugin (same name or same data folder)."},
    {1008, "native plugin library failed to load", nullptr},
    // E2000 — Worlds.
    {2001, "invalid world name",
     "Use 1..64 letters, digits, '_' or '-' (it becomes a folder name)."},
    {2002, "unknown world generator",
     "Valid generators: normal | flat | nether | void | empty."},
    {2003, "world data is missing or unreadable",
     "Restore the world folder from a backup, or remove it to regenerate."},
    {2004, "world failed to load", nullptr},
    // E3000 — Network.
    {3001, "network interface failed to bind", nullptr},
    {3002, "address or port already in use",
     "Stop the other process, or change server-port in server.properties."},
    {3003, "invalid network address configuration",
     "Set server-ip to a local interface address or 0.0.0.0."},
    // E4000 — Blocks and Items.
    {4001, "block id registration collision", nullptr},
    {4002, "invalid item definition", nullptr},
    // E5000 — Internal Server Errors.
    {5001, "unhandled internal exception", nullptr},
    {5002, "client session lost unexpectedly",
     "The client device crashed or lost its uplink; the session report under "
     "crashdumps/ holds this client's final wire traffic."},
    {5003, "internal memory guard tripped",
     "Reduce memory.max-mb, lower view-distance, or check for runaway plugins."},
};

const DiagCodeInfo* findCode(int code) {
    for (const auto& info : kDiagCodes)
        if (info.code == code)
            return &info;
    return nullptr;
}

} // namespace

const char* DiagEngine::codeDescription(int code) {
    const DiagCodeInfo* info = findCode(code);
    return info ? info->description : nullptr;
}

const char* DiagEngine::codeHelp(int code) {
    const DiagCodeInfo* info = findCode(code);
    return info ? info->help : nullptr;
}

void DiagEngine::banner() {
    // Printed from main() before the logger exists (an early logger init
    // would block Server::initLogger from attaching the log file), so this
    // is a direct, color-aware write. The file banner is logged separately
    // from Server::initLogger.
    std::string out;
    if (mcp_terminal_has_color()) {
        out += "\x1b[1m" MCP_TERM_WHITE "CurseStone v" CURSESTONE_API " \"" CURSESTONE_CODENAME
               "\"\x1b[0m\n" MCP_TERM_DIM "Minecraft PE 0.15.10 | Protocol " +
               std::to_string(PROTOCOL_VERSION) + "\x1b[0m\n\n";
    } else {
        out = std::string(CURSESTONE_NAME) + " v" + CURSESTONE_API + " \"" + CURSESTONE_CODENAME +
              "\"\nMinecraft PE 0.15.10 | Protocol " + std::to_string(PROTOCOL_VERSION) + "\n\n";
    }
    fputs(out.c_str(), stdout);
    fflush(stdout);
}

void DiagEngine::stage(const std::string& text) {
    utils::MainLogger::get().info(text);
}

void DiagEngine::stageItem(const std::string& text) {
    utils::MainLogger::get().info(sec('a') + "+ " + sec(kPrimary) + text);
}

void DiagEngine::stageDone(const std::string& text) {
    utils::MainLogger::get().info(text);
}

void DiagEngine::ready(unsigned long boot_ms) {
    utils::MainLogger::get().info(sec('a') + "Server ready! " + sec(kDim) + "(" +
                                  std::to_string(boot_ms) + "ms)");
    const size_t n = g_issues.load(std::memory_order_relaxed);
    if (n > 0)
        utils::MainLogger::get().warn(sec(kWarnColor) + "Startup completed with " +
                                      std::to_string(n) + " issue" + (n == 1 ? "." : "s."));
}

bool DiagEngine::emit(const Diagnostic& d) {
    // Header line — the tagged summary the eye scans for.
    utils::MainLogger::get().log(severityLevel(d.severity), severityColor(d.severity) + d.title);

    // Block — compiler-style detail, pushed verbatim.
    std::string b;
    b += "\n";
    if (!d.file.empty() && d.line > 0) {
        b += "  " + sec(kLocation) + "-->" + sec(kPrimary) + " " + d.file + sec(kDim) + ":" +
             std::to_string(d.line) + "\n\n";
    }
    b += "  " + severityColor(d.severity) +
         (d.code > 0 ? "error[E" + codeStr(d.code) + "]: " : "error: ") +
         sec(kPrimary);
    b += codeDescription(d.code) ? std::string(codeDescription(d.code)) : d.label;
    b += "\n";

    if (!d.file.empty() && d.line > 0) {
        std::string src;
        // The snippet renders only when the reported line actually exists
        // (a Lua error can name the EOF line of a short file). No snippet is
        // always preferred over an invented one.
        if (readSnippetLine(d.file, d.line, src)) {
            const int width = digitsOf(d.line);
            const std::string empty_gutter(width + 1, ' ');
            b += "\n" + sec(kDim) + " " + empty_gutter + "|\n";
            b += sec(kDim) + " " + gutterNum(d.line, width) + " |" + sec(kPrimary) + " " + src +
                 "\n";
            // Carets: col/span when known, otherwise underline the whole line.
            int col = d.col > 0 ? d.col : 1;
            size_t span = d.span > 0 ? (size_t)d.span : (src.size() > (size_t)(col - 1)
                                                             ? src.size() - (size_t)(col - 1)
                                                             : 1);
            if (span == 0)
                span = 1;
            b += sec(kDim) + " " + empty_gutter + "| " + sec(kPrimary) +
                 std::string(col - 1, ' ') + severityColor(d.severity) + std::string(span, '^');
            if (!d.label.empty())
                b += " " + sec(kPrimary) + d.label;
            b += "\n";
            b += sec(kDim) + " " + empty_gutter + "|\n";
        }
    }

    for (const std::string& note : d.notes)
        b += "  " + sec(kDim) + "note: " + sec(kSecondary) + note + "\n";
    const bool has_help = codeHelp(d.code) != nullptr || !d.help.empty();
    if (has_help)
        b += "\n";
    if (const char* default_help = codeHelp(d.code))
        b += "  " + sec(kHelp) + "help: " + sec(kSecondary) + default_help + "\n";
    for (const std::string& help : d.help)
        b += "  " + sec(kHelp) + "help: " + sec(kSecondary) + help + "\n";

    utils::MainLogger::get().raw(b);

    if (d.severity == DiagSeverity::Warn || d.severity == DiagSeverity::Error)
        noteIssue();
    return d.severity == DiagSeverity::Warn || d.severity == DiagSeverity::Error;
}

void DiagEngine::fatal(const Diagnostic& d) {
    Diagnostic f = d;
    f.severity = DiagSeverity::Fatal;
    emit(f);
    utils::MainLogger::get().critical("Startup aborted.");
}

void DiagEngine::noteIssue() {
    g_issues.fetch_add(1, std::memory_order_relaxed);
}

size_t DiagEngine::issues() {
    return g_issues.load(std::memory_order_relaxed);
}

bool DiagEngine::findKeySpan(const std::string& text, const std::string& key, bool ini, int* line,
                             int* col, int* span) {
    // Scan for "key: value" (yaml) or "key=value" (ini) rows. Comments and
    // keys followed by nothing (section headers) never match. The FIRST
    // match wins; the reported line/column/span always come from real file
    // content, so the rendered snippet is honest.
    size_t pos = 0;
    const size_t keyLen = key.size();
    int ln = 1;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();

        size_t s = pos;
        while (s < eol && (text[s] == ' ' || text[s] == '\t'))
            ++s;
        const bool is_comment = s < eol && text[s] == '#';

        if (!is_comment && text.compare(s, keyLen, key) == 0) {
            size_t after = s + keyLen;
            while (after < eol && text[after] == ' ')
                ++after;
            const char delim = ini ? '=' : ':';
            if (after < eol && text[after] == delim) {
                size_t vstart = after + 1;
                while (vstart < eol && (text[vstart] == ' ' || text[vstart] == '\t'))
                    ++vstart;
                size_t vend = eol;
                if (!ini) {
                    // yaml values end at an inline comment.
                    for (size_t i = vstart; i < eol; ++i) {
                        if (text[i] == '#') {
                            vend = i;
                            break;
                        }
                    }
                }
                while (vend > vstart && (text[vend - 1] == ' ' || text[vend - 1] == '\t' ||
                                         text[vend - 1] == '\r'))
                    --vend;
                if (vend > vstart) {
                    if (line)
                        *line = ln;
                    if (col)
                        *col = (int)(vstart - pos) + 1;
                    if (span)
                        *span = (int)(vend - vstart);
                    return true;
                }
            }
        }

        pos = eol + 1;
        ++ln;
    }
    return false;
}

int DiagEngine::luaErrorLine(const std::string& err, const std::string& entry) {
    // Lua chunk errors look like: "src/main.lua:12: unexpected symbol near 'x'".
    const std::string needle = entry + ":";
    size_t at = err.find(needle);
    if (at == std::string::npos)
        return 0;
    size_t num = at + needle.size();
    if (num >= err.size() || err[num] < '0' || err[num] > '9')
        return 0;
    return std::atoi(err.c_str() + num);
}

} // namespace diagnostics
} // namespace cursestone

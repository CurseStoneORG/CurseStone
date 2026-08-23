// SPDX-License-Identifier: LGPL-3.0-or-later
// TextColor.h — Minecraft color/format codes + ANSI terminal colors.
// Mirrors PMMP TextFormat + Terminal in a single C++ header.
// Usage: sendMessage(TC::RED + "Error: " + TC::WHITE + "something happened");
//        sendMessage(TC::GOLD + "--- Help ---" + TC::RESET);
#pragma once
#include <string>

namespace cenisyspe {

// MCPE color/format escape character (section sign in UTF-8)
// In MCPE 0.15.x the client expects \xa7 (Latin-1) not \xc2\xa7 (UTF-8)
namespace TC {
    // Colors (MCPE format codes — sent to client as-is)
    inline const std::string BLACK         = "\xa7""0";
    inline const std::string DARK_BLUE     = "\xa7""1";
    inline const std::string DARK_GREEN    = "\xa7""2";
    inline const std::string DARK_AQUA     = "\xa7""3";
    inline const std::string DARK_RED      = "\xa7""4";
    inline const std::string DARK_PURPLE   = "\xa7""5";
    inline const std::string GOLD          = "\xa7""6";
    inline const std::string GRAY          = "\xa7""7";
    inline const std::string DARK_GRAY     = "\xa7""8";
    inline const std::string BLUE          = "\xa7""9";
    inline const std::string GREEN         = "\xa7""a";
    inline const std::string AQUA          = "\xa7""b";
    inline const std::string RED           = "\xa7""c";
    inline const std::string LIGHT_PURPLE  = "\xa7""d";
    inline const std::string YELLOW        = "\xa7""e";
    inline const std::string WHITE         = "\xa7""f";

    // Formatting
    inline const std::string OBFUSCATED    = "\xa7""k";
    inline const std::string BOLD           = "\xa7""l";
    inline const std::string STRIKETHROUGH  = "\xa7""m";
    inline const std::string UNDERLINE      = "\xa7""n";
    inline const std::string ITALIC         = "\xa7""o";
    inline const std::string RESET         = "\xa7""r";

    // --- ANSI terminal codes (for console output) ---
    // These are used by ConsoleCommandSender to colorize MCPE codes
    // into ANSI escape sequences for the terminal.
    namespace ANSI {
        inline const std::string RESET       = "\x1b[m";
        inline const std::string BOLD        = "\x1b[1m";
        inline const std::string ITALIC      = "\x1b[3m";
        inline const std::string UNDERLINE   = "\x1b[4m";
        inline const std::string STRIKETHROUGH = "\x1b[9m";

        inline const std::string BLACK       = "\x1b[38;5;16m";
        inline const std::string DARK_BLUE  = "\x1b[38;5;19m";
        inline const std::string DARK_GREEN = "\x1b[38;5;34m";
        inline const std::string DARK_AQUA  = "\x1b[38;5;37m";
        inline const std::string DARK_RED   = "\x1b[38;5;124m";
        inline const std::string DARK_PURPLE = "\x1b[38;5;127m";
        inline const std::string GOLD        = "\x1b[38;5;214m";
        inline const std::string GRAY        = "\x1b[38;5;145m";
        inline const std::string DARK_GRAY  = "\x1b[38;5;59m";
        inline const std::string BLUE       = "\x1b[38;5;63m";
        inline const std::string GREEN      = "\x1b[38;5;83m";
        inline const std::string AQUA       = "\x1b[38;5;87m";
        inline const std::string RED        = "\x1b[38;5;203m";
        inline const std::string LIGHT_PURPLE = "\x1b[38;5;207m";
        inline const std::string YELLOW     = "\x1b[38;5;227m";
        inline const std::string WHITE      = "\x1b[38;5;231m";
    }

    // Convert MCPE color codes (\xa7X) to ANSI terminal codes.
    // Used by ConsoleCommandSender when printing to terminal.
    // If ansiEnabled is false, strips all color codes (plain text).
    inline std::string toANSI(const std::string& msg, bool ansiEnabled) {
        if (!ansiEnabled) {
            // Strip all \xa7X codes
            std::string result;
            result.reserve(msg.size());
            for (size_t i = 0; i < msg.size(); ++i) {
                if ((uint8_t)msg[i] == 0xa7 && i + 1 < msg.size()) {
                    ++i; // skip the code char
                } else {
                    result += msg[i];
                }
            }
            return result;
        }
        // Replace \xa7X with ANSI codes
        std::string result;
        result.reserve(msg.size() * 2);
        for (size_t i = 0; i < msg.size(); ++i) {
            if ((uint8_t)msg[i] == 0xa7 && i + 1 < msg.size()) {
                char code = msg[i + 1];
                ++i;
                switch (code) {
                    case '0': result += ANSI::BLACK; break;
                    case '1': result += ANSI::DARK_BLUE; break;
                    case '2': result += ANSI::DARK_GREEN; break;
                    case '3': result += ANSI::DARK_AQUA; break;
                    case '4': result += ANSI::DARK_RED; break;
                    case '5': result += ANSI::DARK_PURPLE; break;
                    case '6': result += ANSI::GOLD; break;
                    case '7': result += ANSI::GRAY; break;
                    case '8': result += ANSI::DARK_GRAY; break;
                    case '9': result += ANSI::BLUE; break;
                    case 'a': case 'A': result += ANSI::GREEN; break;
                    case 'b': case 'B': result += ANSI::AQUA; break;
                    case 'c': case 'C': result += ANSI::RED; break;
                    case 'd': case 'D': result += ANSI::LIGHT_PURPLE; break;
                    case 'e': case 'E': result += ANSI::YELLOW; break;
                    case 'f': case 'F': result += ANSI::WHITE; break;
                    case 'l': case 'L': result += ANSI::BOLD; break;
                    case 'o': case 'O': result += ANSI::ITALIC; break;
                    case 'n': case 'N': result += ANSI::UNDERLINE; break;
                    case 'm': case 'M': result += ANSI::STRIKETHROUGH; break;
                    case 'r': case 'R': result += ANSI::RESET; break;
                    case 'k': case 'K': break; // obfuscated — no ANSI equivalent
                    default: break;
                }
            } else {
                result += msg[i];
            }
        }
        result += ANSI::RESET; // ensure terminal is reset at end
        return result;
    }

    // Strip MCPE color codes from a string (plain text)
    inline std::string clean(const std::string& msg) {
        std::string result;
        result.reserve(msg.size());
        for (size_t i = 0; i < msg.size(); ++i) {
            if ((uint8_t)msg[i] == 0xa7 && i + 1 < msg.size()) {
                ++i; // skip code char
            } else {
                result += msg[i];
            }
        }
        return result;
    }

    // Replace &X color codes with \xa7X (like PMMP colorize)
    inline std::string colorize(const std::string& msg) {
        std::string result;
        result.reserve(msg.size());
        for (size_t i = 0; i < msg.size(); ++i) {
            if (msg[i] == '&' && i + 1 < msg.size()) {
                char next = msg[i + 1];
                if ((next >= '0' && next <= '9') ||
                    (next >= 'a' && next <= 'f') || (next >= 'A' && next <= 'F') ||
                    next == 'k' || next == 'l' || next == 'm' || next == 'n' ||
                    next == 'o' || next == 'r' ||
                    next == 'K' || next == 'L' || next == 'M' || next == 'N' ||
                    next == 'O' || next == 'R') {
                    result += '\xa7';
                    result += next;
                    ++i;
                } else {
                    result += msg[i];
                }
            } else {
                result += msg[i];
            }
        }
        return result;
    }
} // namespace TC

} // namespace cenisyspe

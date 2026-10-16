/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @brief Defines Minecraft §-color/format codes and ANSI terminal colors.
 *
 * Mirrors the reference server TextFormat + Terminal in a single C++ header.
 */
//
// IMPORTANT: § in MCPE 0.15.x TextPacket is encoded as UTF-8 = 0xC2 0xA7
// (two bytes). The reference server uses this encoding throughout. Using the Latin-1
// single-byte 0xA7 causes the client's UTF-8 decoder to fail and the
// whole message becomes INVISIBLE in-game.
#pragma once
#include <string>
#include <cstdint>

namespace cursestone {

// MCPE color/format escape character (UTF-8 sequence for §, 2 bytes)
namespace TC {

// Color / format codes — sent to client as-is (UTF-8 § + code char)
inline const std::string BLACK = "\xc2\xa7"
                                 "0";
inline const std::string DARK_BLUE = "\xc2\xa7"
                                     "1";
inline const std::string DARK_GREEN = "\xc2\xa7"
                                      "2";
inline const std::string DARK_AQUA = "\xc2\xa7"
                                     "3";
inline const std::string DARK_RED = "\xc2\xa7"
                                    "4";
inline const std::string DARK_PURPLE = "\xc2\xa7"
                                       "5";
inline const std::string GOLD = "\xc2\xa7"
                                "6";
inline const std::string GRAY = "\xc2\xa7"
                                "7";
inline const std::string DARK_GRAY = "\xc2\xa7"
                                     "8";
inline const std::string BLUE = "\xc2\xa7"
                                "9";
inline const std::string GREEN = "\xc2\xa7"
                                 "a";
inline const std::string AQUA = "\xc2\xa7"
                                "b";
inline const std::string RED = "\xc2\xa7"
                               "c";
inline const std::string LIGHT_PURPLE = "\xc2\xa7"
                                        "d";
inline const std::string YELLOW = "\xc2\xa7"
                                  "e";
inline const std::string WHITE = "\xc2\xa7"
                                 "f";

inline const std::string OBFUSCATED = "\xc2\xa7"
                                      "k";
inline const std::string BOLD = "\xc2\xa7"
                                "l";
inline const std::string STRIKETHROUGH = "\xc2\xa7"
                                         "m";
inline const std::string UNDERLINE = "\xc2\xa7"
                                     "n";
inline const std::string ITALIC = "\xc2\xa7"
                                  "o";
inline const std::string RESET = "\xc2\xa7"
                                 "r";

// ANSI terminal codes (for console output only)
namespace ANSI {
inline const std::string RESET = "\x1b[m";
inline const std::string BOLD = "\x1b[1m";
inline const std::string ITALIC = "\x1b[3m";
inline const std::string UNDERLINE = "\x1b[4m";
inline const std::string STRIKETHROUGH = "\x1b[9m";

inline const std::string BLACK = "\x1b[38;5;16m";
inline const std::string DARK_BLUE = "\x1b[38;5;19m";
inline const std::string DARK_GREEN = "\x1b[38;5;34m";
inline const std::string DARK_AQUA = "\x1b[38;5;37m";
inline const std::string DARK_RED = "\x1b[38;5;124m";
inline const std::string DARK_PURPLE = "\x1b[38;5;127m";
inline const std::string GOLD = "\x1b[38;5;214m";
inline const std::string GRAY = "\x1b[38;5;145m";
inline const std::string DARK_GRAY = "\x1b[38;5;59m";
inline const std::string BLUE = "\x1b[38;5;63m";
inline const std::string GREEN = "\x1b[38;5;83m";
inline const std::string AQUA = "\x1b[38;5;87m";
inline const std::string RED = "\x1b[38;5;203m";
inline const std::string LIGHT_PURPLE = "\x1b[38;5;207m";
inline const std::string YELLOW = "\x1b[38;5;227m";
inline const std::string WHITE = "\x1b[38;5;231m";
}

inline bool isSectionAt(const std::string& msg, size_t i) {
    return (i + 2 <= msg.size()) && ((uint8_t)msg[i] == 0xc2) && ((uint8_t)msg[i + 1] == 0xa7);
}

// Convert MCPE § codes (UTF-8) to ANSI terminal codes for console.
inline std::string toANSI(const std::string& msg, bool ansiEnabled) {
    std::string result;
    result.reserve(msg.size() * 2);
    for (size_t i = 0; i < msg.size();) {
        if (isSectionAt(msg, i)) {
            char code = msg[i + 2];
            if (ansiEnabled) {
                switch (code) {
                case '0':
                    result += ANSI::BLACK;
                    break;
                case '1':
                    result += ANSI::DARK_BLUE;
                    break;
                case '2':
                    result += ANSI::DARK_GREEN;
                    break;
                case '3':
                    result += ANSI::DARK_AQUA;
                    break;
                case '4':
                    result += ANSI::DARK_RED;
                    break;
                case '5':
                    result += ANSI::DARK_PURPLE;
                    break;
                case '6':
                    result += ANSI::GOLD;
                    break;
                case '7':
                    result += ANSI::GRAY;
                    break;
                case '8':
                    result += ANSI::DARK_GRAY;
                    break;
                case '9':
                    result += ANSI::BLUE;
                    break;
                case 'a':
                case 'A':
                    result += ANSI::GREEN;
                    break;
                case 'b':
                case 'B':
                    result += ANSI::AQUA;
                    break;
                case 'c':
                case 'C':
                    result += ANSI::RED;
                    break;
                case 'd':
                case 'D':
                    result += ANSI::LIGHT_PURPLE;
                    break;
                case 'e':
                case 'E':
                    result += ANSI::YELLOW;
                    break;
                case 'f':
                case 'F':
                    result += ANSI::WHITE;
                    break;
                case 'l':
                case 'L':
                    result += ANSI::BOLD;
                    break;
                case 'o':
                case 'O':
                    result += ANSI::ITALIC;
                    break;
                case 'n':
                case 'N':
                    result += ANSI::UNDERLINE;
                    break;
                case 'm':
                case 'M':
                    result += ANSI::STRIKETHROUGH;
                    break;
                case 'r':
                case 'R':
                    result += ANSI::RESET;
                    break;
                case 'k':
                case 'K':
                    break;
                default:
                    break;
                }
            }
            i += 3;
        } else {
            result += msg[i];
            ++i;
        }
    }
    if (ansiEnabled)
        result += ANSI::RESET;
    return result;
}

inline std::string clean(const std::string& msg) {
    std::string result;
    result.reserve(msg.size());
    for (size_t i = 0; i < msg.size();) {
        if (isSectionAt(msg, i)) {
            i += 3;
        } else {
            result += msg[i];
            ++i;
        }
    }
    return result;
}

inline std::string colorize(const std::string& msg) {
    std::string result;
    result.reserve(msg.size());
    for (size_t i = 0; i < msg.size(); ++i) {
        if (msg[i] == '&' && i + 1 < msg.size()) {
            char next = msg[i + 1];
            if ((next >= '0' && next <= '9') || (next >= 'a' && next <= 'f') ||
                (next >= 'A' && next <= 'F') || next == 'k' || next == 'l' || next == 'm' ||
                next == 'n' || next == 'o' || next == 'r' || next == 'K' || next == 'L' ||
                next == 'M' || next == 'N' || next == 'O' || next == 'R') {
                result += "\xc2\xa7";
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

} // namespace cursestone

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
/**
 * @brief Provides the C++ helpers on top of the C Utils interface.
 *
 * Include this in C++ files; C files include utils/Utils.h.
 */
#pragma once
#include "Utils.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace cursestone::utils {

// Split string on a single-char delimiter (whitespace-aware).
inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string tok;
    for (char c : s) {
        if (c == delim) {
            if (!tok.empty()) {
                out.push_back(tok);
                tok.clear();
            }
        } else {
            tok += c;
        }
    }
    if (!tok.empty())
        out.push_back(tok);
    return out;
}

inline std::string trim(std::string s) {
    auto l = s.find_first_not_of(" \t\r\n");
    auto r = s.find_last_not_of(" \t\r\n");
    return (l == std::string::npos) ? "" : s.substr(l, r - l + 1);
}

inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return s;
}

inline std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return std::toupper(c);
    });
    return s;
}

} // namespace cursestone::utils

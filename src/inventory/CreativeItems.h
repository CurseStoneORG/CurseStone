// SPDX-License-Identifier: LGPL-3.0-or-later
// CreativeItems.h — the static creative-inventory item list.
//
// Mirrors pocketmine/resources/creativeitems.json for MCPE 0.15.x
// (protocol 84). Each entry is {itemId, damage}; count is always 1
// for creative items.
//
// Loading order (mirrors PMMP `Item::initCreativeItems($readFromJson)`):
//   1. Try to load <dataPath>/resources/creativeitems.json at runtime.
//      This is the canonical source — same file PMMP ships.
//   2. On any error (file missing, parse error), fall back to a small
//      hardcoded subset so the inventory is never empty.
//
// The PMMP creativeitems.json is a flat object:
//   {
//     "Cobblestone":{"ID":4,"Damage":0},
//     "Stone Bricks":{"ID":98,"Damage":0},
//     ...
//   }
// Each value has exactly two integer fields: "ID" and "Damage".
#pragma once
#include "item/ItemStack.h"
#include <vector>
#include <cstdint>
#include <string>

namespace cenisyspe::inventory {

struct CreativeItemDef {
    uint16_t id;
    uint8_t  damage;
};

namespace detail {

// Tiny JSON parser specialized for creativeitems.json shape.
// Each top-level entry value is an object with integer "ID" and "Damage".
// We scan for `"ID":<int>` and `"Damage":<int>` pairs inside object
// literals and emit {id, damage} pairs.
struct CreativeItemsLoader {
    std::vector<CreativeItemDef> items;

    bool loadFromFile(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        if (sz <= 0) { fclose(f); return false; }
        fseek(f, 0, SEEK_SET);
        std::string buf;
        buf.resize((size_t)sz);
        size_t rd = fread(&buf[0], 1, (size_t)sz, f);
        fclose(f);
        if (rd == 0) return false;
        buf.resize(rd);
        return parse(buf);
    }

    bool parse(const std::string& src) {
        items.clear();
        size_t i = 0;
        const size_t n = src.size();
        // Find the outer "{" to start
        while (i < n && src[i] != '{') ++i;
        if (i == n) return false;
        ++i; // consume outer {

        while (i < n) {
            // Find next `{` or `}` (end of outer)
            while (i < n && src[i] != '{' && src[i] != '}') ++i;
            if (i == n || src[i] == '}') break;
            ++i;  // consume inner {

            CreativeItemDef def{0, 0};
            bool found_id = false, found_damage = false;
            int brace_depth = 1;
            while (i < n && brace_depth > 0) {
                if (src[i] == '{') { ++brace_depth; ++i; continue; }
                if (src[i] == '}') { --brace_depth; ++i; continue; }
                if (src[i] == '"') {
                    ++i;
                    size_t keyStart = i;
                    while (i < n && src[i] != '"') ++i;
                    std::string key = src.substr(keyStart, i - keyStart);
                    if (i < n) ++i; // consume closing "
                    while (i < n && (src[i] == ' ' || src[i] == '\t' ||
                                      src[i] == '\n' || src[i] == '\r' || src[i] == ':')) ++i;
                    bool neg = false;
                    if (i < n && src[i] == '-') { neg = true; ++i; }
                    long val = 0;
                    bool has_digits = false;
                    while (i < n && src[i] >= '0' && src[i] <= '9') {
                        val = val * 10 + (src[i] - '0');
                        ++i;
                        has_digits = true;
                    }
                    if (has_digits) {
                        if (neg) val = -val;
                        if (key == "ID") {
                            def.id = (uint16_t)(val & 0xFFFF);
                            found_id = true;
                        } else if (key == "Damage") {
                            def.damage = (uint8_t)(val & 0xFF);
                            found_damage = true;
                        }
                    }
                } else {
                    ++i;
                }
            }
            if (found_id) {
                if (!found_damage) def.damage = 0;
                items.push_back(def);
            }
        }
        return !items.empty();
    }
};

} // namespace detail

inline const std::vector<CreativeItemDef>& creativeItemTable(const std::string& dataPath = "") {
    static std::vector<CreativeItemDef> table;
    static bool initialized = false;
    if (initialized) return table;
    initialized = true;

    if (!dataPath.empty()) {
        detail::CreativeItemsLoader loader;
        std::string path = dataPath;
        if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
        path += "resources/creativeitems.json";
        if (loader.loadFromFile(path)) {
            table = std::move(loader.items);
            return table;
        }
    }
    // Fallback — minimal hardcoded list.
    table = {
        {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {17, 0}, {20, 0}, {24, 0},
        {35, 0}, {41, 0}, {42, 0}, {45, 0}, {46, 0}, {49, 0}, {57, 0},
        {58, 0}, {61, 0}, {79, 0}, {80, 0}, {89, 0}, {98, 0},
        {256, 0}, {257, 0}, {258, 0}, {267, 0}, {276, 0}, {277, 0}, {278, 0}, {279, 0},
        {306, 0}, {307, 0}, {308, 0}, {309, 0},
        {310, 0}, {311, 0}, {312, 0}, {313, 0},
        {263, 0}, {264, 0}, {265, 0}, {266, 0}, {280, 0}, {297, 0}, {322, 0},
    };
    return table;
}

inline std::vector<cenisyspe::item::ItemStack> getCreativeItems(const std::string& dataPath = "") {
    const auto& table = creativeItemTable(dataPath);
    std::vector<cenisyspe::item::ItemStack> out;
    out.reserve(table.size());
    for (const auto& e : table) {
        cenisyspe::item::ItemStack s;
        s.id    = e.id;
        s.meta  = e.damage;
        s.count = 1;
        out.push_back(s);
    }
    return out;
}

} // namespace cenisyspe::inventory

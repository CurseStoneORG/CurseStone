// SPDX-License-Identifier: LGPL-3.0-or-later
// Config implementation. Minimal YAML + INI support.
#include "Config.h"
#include "Utils.h"

#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace cenisyspe::utils {

// ---- Node construction ----
static Config::NodePtr makeNode_(Config::Node::Type t) {
    auto n = std::make_unique<Config::Node>();
    n->type = t;
    return n;
}
#define makeNode makeNode_

// ---- INI parser ----
bool Config::loadIni(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    root_ = makeNode(Node::Type::Map);
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Skip comments and empty lines
        size_t firstNonWS = line.find_first_not_of(" \t");
        if (firstNonWS == std::string::npos) continue;
        if (line[firstNonWS] == '#' || line[firstNonWS] == ';') continue;
        if (line[firstNonWS] == '[' && line.back() == ']') {
            section = line.substr(firstNonWS + 1,
                                  line.size() - firstNonWS - 2);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(firstNonWS, eq - firstNonWS);
        std::string val = line.substr(eq + 1);
        // Trim trailing whitespace from key
        while (!key.empty() && std::isspace((unsigned char)key.back())) key.pop_back();
        // Trim leading whitespace from val
        size_t vstart = val.find_first_not_of(" \t");
        if (vstart != std::string::npos) val = val.substr(vstart);
        std::string fullKey = key;
        if (!section.empty()) fullKey = section + "." + key;
        // Store as scalar under the root map.
        Node* cur = root_.get();
        // Simple flat storage: just put it at root.<section>.<key>
        if (!section.empty()) {
            auto it = std::find_if(cur->map.begin(), cur->map.end(),
                [&](const auto& p){ return p.first == section; });
            Node* sec = nullptr;
            if (it == cur->map.end()) {
                cur->map.emplace_back(section, makeNode(Node::Type::Map));
                sec = cur->map.back().second.get();
            } else {
                sec = it->second.get();
            }
            sec->map.emplace_back(key, makeNode(Node::Type::Scalar));
            sec->map.back().second->scalar = val;
        } else {
            cur->map.emplace_back(key, makeNode(Node::Type::Scalar));
            cur->map.back().second->scalar = val;
        }
    }
    return true;
}

bool Config::saveIni(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    if (!root_) return true;
    // Write flat key=value (no section support for output).
    for (const auto& kv : root_->map) {
        if (kv.second->type == Node::Type::Scalar) {
            out << kv.first << "=" << kv.second->scalar << "\n";
        }
    }
    return true;
}

// ---- Minimal YAML parser (nested maps + lists + scalars only) ----
static std::string stripComment(const std::string& s) {
    auto hash = s.find('#');
    if (hash == std::string::npos) return s;
    // Naively treat any '#' as comment start; doesn't handle quoted strings.
    return s.substr(0, hash);
}

static size_t leadingSpaces(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return i;
}

Config::NodePtr Config::parseYamlLines(
        const std::vector<std::string>& lines, size_t& i, size_t indent) {
    auto node = makeNode(Node::Type::Map);
    while (i < lines.size()) {
        const std::string& raw = lines[i];
        std::string line = stripComment(raw);
        // Skip empty lines
        if (line.find_first_not_of(" \t\r") == std::string::npos) {
            ++i;
            continue;
        }
        size_t ind = leadingSpaces(line);
        if (ind < indent) break;     // dedent → return to caller
        if (ind > indent) {
            // Shouldn't happen at the top of the loop, but skip defensively.
            ++i;
            continue;
        }
        std::string body = line.substr(ind);
        // List entry: starts with "- "
        if (body.size() >= 2 && body[0] == '-' && body[1] == ' ') {
            if (node->type != Node::Type::List) {
                node->type = Node::Type::List;
            }
            std::string item = body.substr(2);
            auto child = makeNode(Node::Type::Scalar);
            child->scalar = item;
            // Trim
            while (!child->scalar.empty() &&
                    std::isspace((unsigned char)child->scalar.back())) {
                child->scalar.pop_back();
            }
            node->list.push_back(std::move(child));
            ++i;
            continue;
        }
        // Map entry: "key: value" or "key:"
        auto colon = body.find(':');
        if (colon == std::string::npos) {
            ++i;
            continue;
        }
        std::string key = body.substr(0, colon);
        while (!key.empty() && std::isspace((unsigned char)key.back())) key.pop_back();
        std::string val = body.substr(colon + 1);
        size_t vstart = val.find_first_not_of(" \t");
        if (vstart == std::string::npos) {
            // Block follows; recurse with deeper indent.
            ++i;
            auto child = parseYamlLines(lines, i, indent + 2);
            node->map.emplace_back(key, std::move(child));
        } else {
            std::string scalar = val.substr(vstart);
            while (!scalar.empty() &&
                    std::isspace((unsigned char)scalar.back())) scalar.pop_back();
            // Strip quotes if present
            if (scalar.size() >= 2 && scalar.front() == '"' && scalar.back() == '"') {
                scalar = scalar.substr(1, scalar.size() - 2);
            }
            auto child = makeNode(Node::Type::Scalar);
            child->scalar = scalar;
            node->map.emplace_back(key, std::move(child));
            ++i;
        }
    }
    return node;
}

bool Config::loadYaml(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    size_t i = 0;
    root_ = parseYamlLines(lines, i, 0);
    return true;
}

void Config::writeYamlNode(const Node& n, std::string& out, size_t indent) {
    std::string pad(indent, ' ');
    switch (n.type) {
        case Node::Type::Scalar:
            out += n.scalar;
            out += "\n";
            break;
        case Node::Type::List:
            for (const auto& item : n.list) {
                out += pad;
                out += "- ";
                if (item->type == Node::Type::Scalar) {
                    out += item->scalar;
                    out += "\n";
                } else {
                    out += "\n";
                    writeYamlNode(*item, out, indent + 2);
                }
            }
            break;
        case Node::Type::Map:
            for (const auto& kv : n.map) {
                out += pad;
                out += kv.first;
                out += ":";
                if (kv.second->type == Node::Type::Scalar) {
                    out += " ";
                    out += kv.second->scalar;
                    out += "\n";
                } else {
                    out += "\n";
                    writeYamlNode(*kv.second, out, indent + 2);
                }
            }
            break;
    }
}

bool Config::saveYaml(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    if (!root_) return true;
    std::string buf;
    writeYamlNode(*root_, buf, 0);
    out << buf;
    return true;
}

// ---- Lookups ----
const Config::Node* Config::lookup(const std::string& dottedKey) const {
    if (!root_) return nullptr;
    const Node* cur = root_.get();
    size_t start = 0;
    while (start < dottedKey.size()) {
        size_t dot = dottedKey.find('.', start);
        std::string seg = (dot == std::string::npos)
            ? dottedKey.substr(start)
            : dottedKey.substr(start, dot - start);
        if (cur->type != Node::Type::Map) return nullptr;
        auto it = std::find_if(cur->map.begin(), cur->map.end(),
            [&](const auto& p){ return p.first == seg; });
        if (it == cur->map.end()) return nullptr;
        cur = it->second.get();
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return cur;
}

Config::Node* Config::lookupOrCreate(const std::string& dottedKey) {
    if (!root_) root_ = makeNode(Node::Type::Map);
    Node* cur = root_.get();
    size_t start = 0;
    while (start < dottedKey.size()) {
        size_t dot = dottedKey.find('.', start);
        std::string seg = (dot == std::string::npos)
            ? dottedKey.substr(start)
            : dottedKey.substr(start, dot - start);
        if (cur->type != Node::Type::Map) {
            cur->type = Node::Type::Map;
        }
        auto it = std::find_if(cur->map.begin(), cur->map.end(),
            [&](const auto& p){ return p.first == seg; });
        if (it == cur->map.end()) {
            cur->map.emplace_back(seg, makeNode(Node::Type::Scalar));
            cur = cur->map.back().second.get();
        } else {
            cur = it->second.get();
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return cur;
}

std::string Config::getString(const std::string& key,
                               const std::string& def) const {
    const Node* n = lookup(key);
    if (!n || n->type != Node::Type::Scalar) return def;
    return n->scalar;
}
int64_t Config::getInt(const std::string& key, int64_t def) const {
    std::string s = getString(key, "");
    if (s.empty()) return def;
    try { return std::stoll(s); } catch (...) { return def; }
}
double Config::getDouble(const std::string& key, double def) const {
    std::string s = getString(key, "");
    if (s.empty()) return def;
    try { return std::stod(s); } catch (...) { return def; }
}
bool Config::getBool(const std::string& key, bool def) const {
    std::string s = getString(key, "");
    if (s.empty()) return def;
    if (s == "true" || s == "yes" || s == "1") return true;
    if (s == "false" || s == "no" || s == "0") return false;
    return def;
}
std::vector<std::string> Config::getStringList(
        const std::string& key,
        const std::vector<std::string>& def) const {
    const Node* n = lookup(key);
    if (!n || n->type != Node::Type::List) return def;
    std::vector<std::string> out;
    out.reserve(n->list.size());
    for (const auto& item : n->list) {
        if (item->type == Node::Type::Scalar) out.push_back(item->scalar);
    }
    return out;
}

void Config::setString(const std::string& key, const std::string& v) {
    Node* n = lookupOrCreate(key);
    n->type = Node::Type::Scalar;
    n->scalar = v;
}
void Config::setInt(const std::string& key, int64_t v) {
    setString(key, std::to_string(v));
}
void Config::setBool(const std::string& key, bool v) {
    setString(key, v ? "true" : "false");
}

} // namespace cenisyspe::utils

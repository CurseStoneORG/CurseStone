// SPDX-License-Identifier: LGPL-3.0-or-later
// Config — YAML / INI / JSON loader for the server config file.
// Implements a minimal subset of YAML (nested maps, lists, scalars, comments)
// sufficient to parse `resources/atomikpe.yml`. For full YAML support,
// future phases may vendor yaml-cpp.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <memory>
#include <optional>
#include <cstdint>

namespace atomikpe::utils {

class Config {
public:
    Config() = default;
    ~Config() = default;

    // A YAML value: a scalar string, a list of children, or a map of children.
    struct Node;
    using NodePtr = std::unique_ptr<Node>;
    struct Node {
        enum class Type { Scalar, List, Map };
        Type type = Type::Scalar;
        std::string scalar;
        std::vector<NodePtr>           list;
        std::vector<std::pair<std::string, NodePtr>> map;  // insertion-ordered
    };

    // Load YAML from disk. Returns true on success.
    bool loadYaml(const std::string& path);
    // Save YAML to disk.
    bool saveYaml(const std::string& path) const;
    // Load INI (key=value) from disk.
    bool loadIni(const std::string& path);
    // Save INI to disk.
    bool saveIni(const std::string& path) const;

    // Generic getters with default fallback.
    std::string getString(const std::string& dottedKey,
                          const std::string& def = "") const;
    int64_t     getInt(const std::string& dottedKey,
                       int64_t def = 0) const;
    double      getDouble(const std::string& dottedKey,
                          double def = 0.0) const;
    bool        getBool(const std::string& dottedKey,
                       bool def = false) const;
    std::vector<std::string> getStringList(
        const std::string& dottedKey,
        const std::vector<std::string>& def = {}) const;

    void setString(const std::string& dottedKey, const std::string& v);
    void setInt(const std::string& dottedKey, int64_t v);
    void setBool(const std::string& dottedKey, bool v);

private:
    NodePtr root_;

    // Lookup by dotted key (e.g. "server.port"). Returns nullptr if missing.
    const Node* lookup(const std::string& dottedKey) const;
    Node*       lookupOrCreate(const std::string& dottedKey);

    // Recursive YAML parser (minimal).
    static NodePtr parseYamlLines(const std::vector<std::string>& lines,
                                   size_t& i, size_t indent);
    static void   writeYamlNode(const Node& n, std::string& out,
                                 size_t indent);
};

} // namespace atomikpe::utils

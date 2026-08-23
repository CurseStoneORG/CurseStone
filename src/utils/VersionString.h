// SPDX-License-Identifier: LGPL-3.0-or-later
// VersionString — semantic version wrapper used by Server/Plugin manifests.
#pragma once

#include <string>
#include <cstdint>

namespace cenisyspe::utils {

class VersionString {
public:
    VersionString() = default;
    explicit VersionString(const std::string& s);

    int major() const { return major_; }
    int minor() const { return minor_; }
    int patch() const { return patch_; }
    const std::string& suffix() const { return suffix_; }

    // True if this version is >= `other`.
    bool operator>=(const VersionString& other) const;
    bool operator==(const VersionString& other) const;
    bool operator<(const VersionString& other) const;

    std::string toString() const;

private:
    int         major_ = 0;
    int         minor_ = 0;
    int         patch_ = 0;
    std::string suffix_;
};

} // namespace cenisyspe::utils

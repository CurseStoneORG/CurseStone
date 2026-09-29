// SPDX-License-Identifier: LGPL-3.0-or-later
// VersionString implementation.
#include "VersionString.h"

#include <cctype>
#include <sstream>

namespace cursestone::utils {

VersionString::VersionString(const std::string& s) {
    size_t i = 0;
    int*  cur = &major_;
    int  acc = 0;
    bool haveDigit = false;
    while (i < s.size()) {
        char c = s[i];
        if (std::isdigit((unsigned char)c)) {
            acc = acc * 10 + (c - '0');
            haveDigit = true;
        } else if (c == '.') {
            if (haveDigit) {
                *cur = acc;
                acc = 0;
                haveDigit = false;
                if (cur == &major_)       cur = &minor_;
                else if (cur == &minor_) cur = &patch_;
                else break;
            }
        } else {
            break;
        }
        ++i;
    }
    if (haveDigit) *cur = acc;
    if (i < s.size()) suffix_ = s.substr(i);
}

bool VersionString::operator>=(const VersionString& o) const {
    if (major_ != o.major_) return major_ > o.major_;
    if (minor_ != o.minor_) return minor_ > o.minor_;
    return patch_ >= o.patch_;
}
bool VersionString::operator==(const VersionString& o) const {
    return major_ == o.major_ && minor_ == o.minor_ && patch_ == o.patch_;
}
bool VersionString::operator<(const VersionString& o) const {
    return !(*this >= o);
}

std::string VersionString::toString() const {
    std::ostringstream o;
    o << major_ << "." << minor_ << "." << patch_;
    if (!suffix_.empty()) o << suffix_;
    return o.str();
}

} // namespace cursestone::utils

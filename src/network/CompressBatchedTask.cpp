// SPDX-License-Identifier: LGPL-3.0-or-later
// CompressBatchedTask.cpp — zlib compress on the async pool.
#include "CompressBatchedTask.h"

#include <zlib.h>

namespace mcppeserver {
namespace network {

void CompressBatchedTask::onRun() {
    uLong srcLen = static_cast<uLong>(payload_.size());
    uLong bound  = compressBound(srcLen);
    std::vector<uint8_t> tmp(bound);
    uLongf dstLen = bound;
    int r = compress2(tmp.data(), &dstLen,
                       payload_.data(), srcLen,
                       level_ >= 1 && level_ <= 9 ? level_ : 7);
    if (r != Z_OK) {
        result_.clear();
        return;
    }
    result_.assign(tmp.data(), tmp.data() + dstLen);
}

void CompressBatchedTask::onCompletion() {
    if (cb_) cb_(std::move(result_));
}

} // namespace network
} // namespace mcppeserver

// SPDX-License-Identifier: LGPL-3.0-or-later
// CompressBatchedTask — background zlib compression of a batch of
// sub-packets. Run on the AsyncPool. C++.
#pragma once

#include "../scheduler/Task.h"
#include "protocol/BatchPacket.h"
#include <vector>
#include <functional>
#include <memory>

namespace cursestone::network {

class CompressBatchedTask : public scheduler::AsyncTask {
public:
    using CompletionCb = std::function<void(std::vector<uint8_t>)>;

    CompressBatchedTask(std::vector<uint8_t> payload, int level,
                          CompletionCb cb)
        : payload_(std::move(payload)), level_(level), cb_(std::move(cb)) {}

    void onRun() override;          // worker thread: do zlib compression
    void onCompletion() override;    // main thread: deliver result via cb_

private:
    std::vector<uint8_t> payload_;
    int                   level_;
    CompletionCb          cb_;
    std::vector<uint8_t>  result_;   // populated by onRun()
};

} // namespace cursestone::network

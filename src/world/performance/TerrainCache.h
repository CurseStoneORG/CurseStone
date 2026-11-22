/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// TerrainCache — LRU cache for immutable terrain chunks with in-flight
// build coordination.
//
// Measured on the stock generator (spawn pregen, 8 workers): 1819 terrain
// builds for 1088 chunks, because two workers that populate adjacent
// chunks at the same time can both miss the same neighbour and build its
// terrain in parallel — the loser is discarded whole. The in-flight slot
// makes concurrent acquires of the same key share ONE build: the first
// caller builds outside every lock, the rest wait on the shard's
// condition variable and get the finished chunk.
//
// Locks are sharded so workers touching different keys rarely contend,
// and LRU eviction is O(1) per eviction (the old stamp scan walked the
// whole map every time the cache overflowed).
//
// Waiters never hold references into the map across a wake: they wait
// on a key-based predicate, because the slot they are waiting for can
// be erased (failed build) or evicted before they are scheduled again.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace cursestone::performance {

template <class T>
class TerrainCache {
  public:
    using Ptr = std::shared_ptr<T>;

    explicit TerrainCache(size_t capacity = 512)
        : per_shard_cap_((capacity ? capacity : 1 + kShards - 1) / kShards) {
        if (per_shard_cap_ < 1)
            per_shard_cap_ = 1;
    }

    // Returns the cached value for `key`, building it with `build` on a
    // cache miss. Exactly one caller runs `build` per miss; concurrent
    // acquires of the same key block until the build publishes (or
    // fails, in which case one waiter retries as the new builder).
    template <class Build>
    Ptr acquire(uint64_t key, Build&& build) {
        Shard& s = *shard(key);
        std::unique_lock<std::mutex> lk(s.mtx);
        for (;;) {
            auto it = s.map.find(key);
            if (it != s.map.end() && it->second.state == Slot::Ready) {
                s.order.splice(s.order.begin(), s.order, it->second.lru);
                return it->second.value;
            }
            if (it != s.map.end()) {
                // Someone is building this key right now. The predicate
                // looks the slot up by key every wake: the entry can be
                // erased (failed build) while we sleep, so capturing a
                // reference into the map would dangle.
                s.cv.wait(lk, [&s, key] {
                    auto jt = s.map.find(key);
                    return jt == s.map.end() || jt->second.state == Slot::Ready;
                });
                it = s.map.find(key);
                if (it != s.map.end() && it->second.state == Slot::Ready)
                    return it->second.value;
                continue; // build failed — slot is gone, retry as builder
            }

            // Miss: mark the slot in-flight BEFORE releasing the lock.
            s.map.emplace(key, Slot{Slot::Building, nullptr, s.order.end()});
            lk.unlock();
            Ptr value;
            try {
                value = build();
            } catch (...) {
                lk.lock();
                s.map.erase(key);
                lk.unlock();
                s.cv.notify_all();
                throw;
            }

            lk.lock();
            auto ins = s.map.find(key);
            if (ins == s.map.end()) {
                // clear() removed the slot while we were building.
                lk.unlock();
                s.cv.notify_all();
                return value;
            }
            Slot& slot = ins->second;
            slot.value = std::move(value);
            slot.state = Slot::Ready;
            s.order.push_front(key);
            slot.lru = s.order.begin();
            while (s.order.size() > per_shard_cap_)
                evictOldestLocked(s);
            // Copy the pointer under the lock: after unlock another
            // worker may evict the entry and destroy this shared_ptr.
            Ptr published = slot.value;
            lk.unlock();
            s.cv.notify_all();
            return published;
        }
    }

    void clear() {
        for (Shard& s : shards_) {
            std::lock_guard<std::mutex> lk(s.mtx);
            // Ready entries only: in-flight slots belong to a builder
            // that will publish into a vanished map entry and see the
            // hand-back path above.
            for (auto it = s.map.begin(); it != s.map.end();) {
                if (it->second.state == Slot::Ready) {
                    if (it->second.lru != s.order.end())
                        s.order.erase(it->second.lru);
                    it = s.map.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    size_t capacity() const { return per_shard_cap_ * kShards; }

  private:
    struct Slot {
        enum State : uint8_t { Building, Ready };
        State state;
        Ptr value;
        typename std::list<uint64_t>::iterator lru;
    };
    struct Shard {
        std::mutex mtx;
        std::condition_variable cv;
        std::unordered_map<uint64_t, Slot> map;
        std::list<uint64_t> order; // front = most recent
    };

    static size_t shardIndex(uint64_t key) {
        uint64_t h = key * 0x9e3779b97f4a7c15ULL;
        return (size_t)(h >> 58) & (kShards - 1);
    }
    Shard* shard(uint64_t key) { return &shards_[shardIndex(key)]; }

    void evictOldestLocked(Shard& s) {
        // Every key in `order` has a Ready map entry (in-flight slots
        // are not in the list), so one pass always frees a slot.
        while (!s.order.empty()) {
            const uint64_t oldest = s.order.back();
            auto it = s.map.find(oldest);
            s.order.pop_back();
            if (it != s.map.end() && it->second.state == Slot::Ready) {
                s.map.erase(it);
                return;
            }
        }
    }

    static constexpr size_t kShards = 4;
    size_t per_shard_cap_;
    Shard shards_[kShards];
};

} // namespace cursestone::performance

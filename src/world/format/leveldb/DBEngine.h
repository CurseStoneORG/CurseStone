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
// SPDX-License-Identifier: LGPL-3.0-or-later
// DBEngine — a self-contained, real-format LevelDB engine.
//
// Creates and maintains the same file set the real MCPE client produces
// inside `<world>/db`: CURRENT, LOCK, MANIFEST-<n>, <n>.log (WAL) and
// <n>.ldb (tables) — no external library involved. Worlds written by the
// real device (snappy tables) and by PocketMine (zlib tables) open here,
// and the files this engine writes are ordinary LevelDB tables any reader
// of the format understands.
//
// Layout it maintains:
//   db/CURRENT          -> "MANIFEST-000002\n"
//   db/LOCK             -> flock()ed exclusively while open
//   db/MANIFEST-<n>     -> log-format file holding the full VersionEdit
//   db/<n>.log          -> WAL of batches not yet flushed to a table
//   db/<n>.ldb          -> SSTables (level 0 from flushes, level 1 from
//                          the periodic full compaction)
//
// Durability contract (what a server actually needs):
//   * every accepted batch is in the WAL before put_batch() returns
//     (process-crash safe),
//   * flush() turns the memtable into a table + rotates the WAL (the
//     manifest is rewritten and CURRENT swapped atomically before the old
//     WAL is deleted, so a crash in between replays, never loses),
//   * full compaction merges every table when level-0 count crosses the
//     trigger, keeping the file set bounded.
//
// Threading: one internal mutex. The provider funnels writes through its
// background worker; reads may come from the main thread concurrently.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "DBFormat.h"
#include "DBTable.h"

namespace cursestone::world::format::leveldb {

// Memtable ordering: internal_key_less as a stateless comparator type.
struct InternalKeyLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return db::internal_key_less(a, b);
    }
};

class DBEngine {
  public:
    DBEngine() = default;
    ~DBEngine();
    DBEngine(const DBEngine&) = delete;
    DBEngine& operator=(const DBEngine&) = delete;

    // Opens (or creates) the database rooted at `db_dir`. Fails when the
    // LOCK file is held by another process. On success the database is
    // ready for get/put_batch/scan.
    bool open(const std::string& db_dir, std::string* err);

    // Flushes any live memtable to a table (safety), closes files, releases
    // the LOCK.
    void close();

    bool is_open() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return opened_;
    }

    // One write batch. nullopt value = deletion tombstone. All ops land
    // under one sequence number and one WAL record.
    bool put_batch(
        const std::vector<std::pair<std::string, std::optional<std::string>>>& ops);

    // Point lookup (newest visible version). Returns nullopt when the key
    // is absent or deleted.
    std::optional<std::string> get(const std::string& user_key) const;

    // Calls sink(user_key, value) for every live key starting with
    // `prefix`, in key order, newest version first. Tombstoned keys are
    // skipped.
    void scan(const std::string& prefix,
              const std::function<void(const std::string&, const std::string&)>& sink)
        const;

    // Approximate bytes waiting in the memtable (flush trigger input).
    size_t memtable_bytes() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return memtable_bytes_;
    }

    // Memtable -> new level-0 table + WAL rotation + manifest rewrite.
    // Returns true when there was nothing to flush or the flush succeeded.
    bool flush(std::string* err = nullptr);

    const std::string& dir() const {
        return dir_;
    }

  private:
    // --- manifest & files (all called with mtx_ held unless noted) ------
    bool write_manifest_locked(std::string* err);
    bool replay_logs_locked(uint64_t from_number, uint64_t* max_seq, std::string* err);
    bool flush_locked(std::string* err);
    bool compact_locked(std::string* err); // full merge of every table
    std::string table_path(uint64_t number) const;
    std::string log_path(uint64_t number) const;
    bool read_file_bytes(const std::string& path, std::vector<uint8_t>* out) const;
    bool write_file_bytes(const std::string& path, const uint8_t* data, size_t len,
                          bool sync) const;

    // --- state (guarded by mtx_) ----------------------------------------
    mutable std::mutex mtx_;
    bool opened_ = false;
    std::string dir_;

    // Memtable: internal keys (with sequence + type) sorted newest-first
    // per user key. Value "" + kTypeDeletion trailer = tombstone.
    std::map<std::string, std::string, InternalKeyLess> memtable_;
    size_t memtable_bytes_ = 0;

    std::vector<db::FileMeta> files_; // all live tables, any level
    uint64_t log_number_ = 0;
    uint64_t next_file_number_ = 1;
    uint64_t last_sequence_ = 0;

    FILE* log_file_ = nullptr;
    uint64_t wal_bytes_ = 0; // bytes already written to the current WAL
    int lock_fd_ = -1;
    uint64_t manifest_number_ = 0;
    size_t memtable_batch_count_ = 0; // batches since last flush
};

} // namespace cursestone::world::format::leveldb

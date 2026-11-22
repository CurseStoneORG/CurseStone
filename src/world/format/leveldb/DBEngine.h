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
#include <memory>
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

// A cached read-only view of one live table. Tables are immutable once
// written, so the image and the parsed reader are shared safely across
// threads WITHOUT the engine mutex: point lookups and scans only take
// mtx_ briefly to fetch the shared view (and, on a miss, to publish the
// freshly loaded one). POSIX maps the file read-only (page-cache backed,
// no RSS amplification); platforms without mmap cache the bytes read
// once into the heap buffer.
struct OpenTable {
    uint64_t number = 0;
    size_t size = 0;
    const uint8_t* image = nullptr; // mapped view when mapped, else heap.data()
    std::vector<uint8_t> heap;      // populated when the file is not mapped
    bool mapped = false;
    db::TableReader reader;
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

    // --- cached table views --------------------------------------------
    // NOT called with mtx_ held for the load path: a cache miss maps/
    // reads the file with the mutex RELEASED (the whole point — the old
    // code read whole tables under mtx_, so every chunk load serialized
    // behind flushes and compactions). Returns a shared view that stays
    // valid even after the file is pruned from the cache or deleted by a
    // later compaction (the caller holds its own reference).
    std::shared_ptr<const OpenTable> get_table(uint64_t number) const;
    // One point-lookup attempt over the memtable + candidate tables (see
    // get() for the retry-on-generation-change wrapper).
    bool get_once(const std::string& user_key, const std::string& target,
                  uint64_t* gen, std::optional<std::string>* out, bool* retry) const;
    // Drops cached views of tables that no longer belong to the live file
    // set (after a compaction) and enforces the cache byte cap. Called
    // with mtx_ held; entries still referenced by a reader stay alive via
    // shared_ptr until the reader is done with them.
    void prune_table_cache_locked() const;

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

    // Cached immutable table views (see OpenTable). Mutated only under
    // mtx_ (insert/prune — mutable because get() is a const interface
    // that lazily fills the cache); the views themselves are used
    // lock-free.
    mutable std::map<uint64_t, std::shared_ptr<const OpenTable>> open_tables_;
    mutable size_t open_tables_bytes_ = 0;
    // Bumped on every files_ change (flush publish, compaction publish).
    // Point lookups snapshot the candidate list + generation and RETRY
    // when the generation moved underneath them: a compaction that runs
    // mid-lookup relocates the data into the merged table, and a stale
    // candidate list alone could report a missing key for data that is
    // very much alive (a read returning a pre-write value is legitimate
    // snapshot semantics; a read missing a committed key is not).
    mutable uint64_t files_generation_{0};
    // Generous cap: a 2500-chunk MCPE db is ~60 MB of tables. Entries are
    // evicted FIFO (oldest unreferenced first) once this is exceeded.
    static constexpr size_t kTableCacheMaxBytes = 512u << 20;
};

} // namespace cursestone::world::format::leveldb

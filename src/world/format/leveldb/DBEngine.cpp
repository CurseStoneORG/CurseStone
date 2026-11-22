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
// DBEngine.cpp — real-format LevelDB engine implementation.
#include "DBEngine.h"

#include <algorithm>
#include <cerrno>
#include <fcntl.h> // O_*/_O_* for the LOCK descriptor on both platforms
#include <cstdio>
#include <cstring>

#include "../../../utils/PlatformCompat.h"
#include "../../../world/performance/SaveProfiler.h"

#if !defined(_WIN32)
#include <sys/file.h> // ::flock and the LOCK_* verbs for the whole-file lock
#include <unistd.h>   // ::close, ::fsync, ::fileno
#endif

namespace cursestone::world::format::leveldb {

using db::FileMeta;
using db::TableEntry;

namespace {

// Flush the memtable to a new table once it holds roughly this much.
// ~48 chunks per 4 MiB at legacy-record size; a table write of that size
// costs a few ms on the save worker, well off the tick. The provider asks
// memtable_bytes() after each batch and calls flush() at this threshold.
constexpr size_t kMemtableFlushBytes = 4u << 20; // 4 MiB
// Level-0 tables tolerated before everything is merged into one.
constexpr size_t kLevel0CompactionTrigger = 8;

// Platform shims for the engine's four POSIX-isms. PlatformCompat.h already
// maps mkdir/stat/opendir/readdir onto their Windows counterparts; what it
// does not cover is the advisory whole-file lock on db/LOCK, the durability
// flush and the CRT fd pair around them. The _-prefixed CRT spellings are
// used directly on Windows to stay clear of the deprecated POSIX names.
#if defined(_WIN32)
// flock() has no CRT equivalent: an exclusive, non-blocking LockFileEx over
// the whole 64-bit range is the strict Win32 counterpart (MAXDWORD/MAXDWORD
// is the documented "lock the entire file" idiom), released by UnlockFileEx
// on the very same range.
inline bool db_lock_exclusive(int fd) {
    HANDLE h = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE)
        return false;
    OVERLAPPED ov{};
    return ::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                        MAXDWORD, MAXDWORD, &ov) != 0;
}
inline void db_unlock(int fd) {
    HANDLE h = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE)
        return;
    OVERLAPPED ov{};
    ::UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, &ov);
}
inline int db_open_fd(const char* path) {
    return ::_open(path, _O_RDWR | _O_CREAT | _O_BINARY, _S_IREAD | _S_IWRITE);
}
inline int db_close_fd(int fd) { return ::_close(fd); }
// fsync() through the CRT: _commit writes the descriptor's buffers to disk.
inline int db_sync_file(FILE* fp) { return ::_commit(::_fileno(fp)); }
#else
inline bool db_lock_exclusive(int fd) { return ::flock(fd, LOCK_EX | LOCK_NB) == 0; }
inline void db_unlock(int fd) { ::flock(fd, LOCK_UN); }
inline int db_open_fd(const char* path) { return ::open(path, O_RDWR | O_CREAT, 0644); }
inline int db_close_fd(int fd) { return ::close(fd); }
inline int db_sync_file(FILE* fp) { return ::fsync(::fileno(fp)); }
#endif

bool ensure_dir(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0)
        return S_ISDIR(st.st_mode);
    if (errno != ENOENT)
        return false;
    return ::mkdir(path.c_str(), 0755) == 0;
}

std::string format_number(uint64_t n) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06llu", (unsigned long long)n);
    return buf;
}

// ---- WriteBatch encoding (the real leveldb layout) ------------------------
//   [8 bytes sequence, LE][4 bytes count, LE]
//   per op: 1-byte type; kTypeValue: varint32 len + bytes;
//           kTypeDeletion: nothing
void encode_batch(uint64_t seq, const std::vector<std::pair<std::string,
                  std::optional<std::string>>>& ops, std::vector<uint8_t>* out) {
    for (int i = 0; i < 8; ++i)
        out->push_back((uint8_t)(seq >> (8 * i)));
    uint32_t count = (uint32_t)ops.size();
    out->push_back((uint8_t)count);
    out->push_back((uint8_t)(count >> 8));
    out->push_back((uint8_t)(count >> 16));
    out->push_back((uint8_t)(count >> 24));
    for (const auto& [key, value] : ops) {
        if (value.has_value()) {
            // Real leveldb WriteBatch entry: type + varstring(key) +
            // varstring(value).
            out->push_back(db::kTypeValue);
            db::put_var_u32(out, (uint32_t)key.size());
            out->insert(out->end(), key.begin(), key.end());
            db::put_var_u32(out, (uint32_t)value->size());
            out->insert(out->end(), value->begin(), value->end());
        } else {
            out->push_back(db::kTypeDeletion);
            db::put_var_u32(out, (uint32_t)key.size());
            out->insert(out->end(), key.begin(), key.end());
        }
    }
}

// Inverse of encode_batch. Returns false on malformed input (torn tail).
bool decode_batch(const uint8_t* data, size_t len, uint64_t* seq,
                  std::vector<std::pair<std::string, std::optional<std::string>>>*
                      ops) {
    if (len < 12)
        return false;
    *seq = 0;
    for (int i = 0; i < 8; ++i)
        *seq |= (uint64_t)data[i] << (8 * i);
    uint32_t count = (uint32_t)data[8] | ((uint32_t)data[9] << 8) |
                     ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
    size_t pos = 12;
    ops->clear();
    ops->reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (pos >= len)
            return false;
        uint8_t type = data[pos++];
        uint32_t klen = 0;
        if (!db::get_var_u32(data, len, &pos, &klen) || pos + klen > len)
            return false;
        std::string key((const char*)data + pos, klen);
        pos += klen;
        if (type == db::kTypeValue) {
            uint32_t vlen = 0;
            if (!db::get_var_u32(data, len, &pos, &vlen) || pos + vlen > len)
                return false;
            ops->emplace_back(std::move(key),
                              std::string((const char*)data + pos, vlen));
            pos += vlen;
        } else if (type == db::kTypeDeletion) {
            ops->emplace_back(std::move(key), std::nullopt);
        } else {
            return false;
        }
    }
    return true;
}

} // namespace

DBEngine::~DBEngine() {
    close();
}

// ---------------------------------------------------------------------------
// Paths + small file helpers
// ---------------------------------------------------------------------------

std::string DBEngine::table_path(uint64_t number) const {
    return dir_ + "/" + format_number(number) + ".ldb";
}

std::string DBEngine::log_path(uint64_t number) const {
    return dir_ + "/" + format_number(number) + ".log";
}

bool DBEngine::read_file_bytes(const std::string& path,
                               std::vector<uint8_t>* out) const {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp)
        return false;
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz < 0) {
        std::fclose(fp);
        return false;
    }
    out->resize((size_t)sz);
    bool ok = (sz == 0) || (std::fread(out->data(), 1, (size_t)sz, fp) == (size_t)sz);
    std::fclose(fp);
    return ok;
}

bool DBEngine::write_file_bytes(const std::string& path, const uint8_t* data,
                                size_t len, bool sync) const {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp)
        return false;
    bool ok = (len == 0) || (std::fwrite(data, 1, len, fp) == len);
    if (ok && sync)
        ok = (std::fflush(fp) == 0) && (db_sync_file(fp) == 0);
    std::fclose(fp);
    return ok;
}

// ---------------------------------------------------------------------------
// Manifest — rewritten in full on every change, CURRENT swapped atomically.
// A manifest is small (one record per live table), so a full rewrite per
// change costs nothing and removes the torn-append failure mode entirely.
// ---------------------------------------------------------------------------

bool DBEngine::write_manifest_locked(std::string* err) {
    db::VersionEdit edit;
    edit.comparator = db::kComparatorName;
    edit.log_number = log_number_;
    edit.next_file_number = next_file_number_;
    edit.last_sequence = last_sequence_;
    edit.added_files = files_;

    std::vector<uint8_t> payload;
    edit.encode(&payload);
    std::vector<uint8_t> manifest;
    db::log_append_record(&manifest, payload.data(), payload.size());

    uint64_t number = next_file_number_++;
    std::string path = dir_ + "/MANIFEST-" + format_number(number);
    if (!write_file_bytes(path, manifest.data(), manifest.size(), /*sync=*/true)) {
        if (err)
            *err = "failed writing " + path;
        return false;
    }

    // CURRENT via tmp + rename: readers never see a half-written name.
    std::string tmp = dir_ + "/CURRENT.tmp";
    std::string content = "MANIFEST-" + format_number(number) + "\n";
    if (!write_file_bytes(tmp, (const uint8_t*)content.data(), content.size(),
                          /*sync=*/true)) {
        if (err)
            *err = "failed writing CURRENT.tmp";
        return false;
    }
    if (!db::replace_file(tmp, dir_ + "/CURRENT")) {
        if (err)
            *err = "failed renaming CURRENT.tmp";
        return false;
    }

    // The previous manifest is unreachable now — remove it.
    if (manifest_number_ != 0)
        std::remove((dir_ + "/MANIFEST-" + format_number(manifest_number_)).c_str());
    manifest_number_ = number;
    return true;
}

// ---------------------------------------------------------------------------
// Open / recovery
// ---------------------------------------------------------------------------

bool DBEngine::open(const std::string& db_dir, std::string* err) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (opened_)
        return true;
    auto fail = [&](const std::string& msg) {
        if (err)
            *err = msg;
        return false;
    };

    if (!ensure_dir(db_dir))
        return fail("cannot create db directory " + db_dir);
    dir_ = db_dir;

    // Exclusive LOCK for the lifetime of the handle (real leveldb does the
    // same; two processes must never write one db/).
    std::string lock_path = dir_ + "/LOCK";
    lock_fd_ = db_open_fd(lock_path.c_str());
    if (lock_fd_ < 0)
        return fail("cannot open LOCK file");
    if (!db_lock_exclusive(lock_fd_)) {
        db_close_fd(lock_fd_);
        lock_fd_ = -1;
        return fail("db is already locked by another process (LOCK)");
    }

    // Every failure from here on must release the lock before returning.
    auto fail_locked = [&](const std::string& msg) {
        if (log_file_) {
            std::fclose(log_file_);
            log_file_ = nullptr;
        }
        if (lock_fd_ >= 0) {
            db_unlock(lock_fd_);
            db_close_fd(lock_fd_);
            lock_fd_ = -1;
        }
        if (err)
            *err = msg;
        return false;
    };

    // Read CURRENT (optional: a fresh db has none yet).
    std::string manifest_name;
    {
        std::vector<uint8_t> cur;
        if (read_file_bytes(dir_ + "/CURRENT", &cur) && !cur.empty()) {
            manifest_name.assign(cur.begin(), cur.end());
            while (!manifest_name.empty() &&
                   (manifest_name.back() == '\n' || manifest_name.back() == '\r' ||
                    manifest_name.back() == ' '))
                manifest_name.pop_back();
            if (manifest_name.rfind("MANIFEST-", 0) != 0)
                return fail_locked("CURRENT does not name a MANIFEST");
        }
    }

    uint64_t max_file_number = 0;
    if (!manifest_name.empty()) {
        std::vector<uint8_t> image;
        if (!read_file_bytes(dir_ + "/" + manifest_name, &image))
            return fail_locked("cannot read " + manifest_name);
        db::LogReader reader(image);
        const uint8_t* payload = nullptr;
        size_t plen = 0;
        db::VersionEdit combined;
        bool any = false;
        while (reader.next(&payload, &plen)) {
            db::VersionEdit edit;
            if (!edit.decode(payload, plen))
                return fail_locked("corrupt VersionEdit in " + manifest_name);
            if (!edit.comparator.empty())
                combined.comparator = edit.comparator;
            if (edit.log_number)
                combined.log_number = edit.log_number;
            if (edit.prev_log_number)
                combined.prev_log_number = edit.prev_log_number;
            if (edit.next_file_number)
                combined.next_file_number = edit.next_file_number;
            if (edit.last_sequence)
                combined.last_sequence = edit.last_sequence;
            for (uint64_t df : edit.deleted_files) {
                auto it = std::find_if(combined.added_files.begin(),
                                       combined.added_files.end(), [&](const FileMeta& f) {
                                       return (((uint64_t)f.level << 32) | f.number) == df;
                                   });
                if (it != combined.added_files.end())
                    combined.added_files.erase(it);
            }
            for (const FileMeta& f : edit.added_files)
                combined.added_files.push_back(f);
            any = true;
        }
        if (!any)
            return fail_locked("manifest holds no edits: " + manifest_name);
        if (!combined.comparator.empty() &&
            combined.comparator != db::kComparatorName)
            return fail_locked("unknown comparator: " + combined.comparator);
        log_number_ = combined.log_number;
        last_sequence_ = combined.last_sequence;
        files_ = std::move(combined.added_files);
        {
            // Remember the manifest's own file number for later cleanup.
            uint64_t n = 0;
            for (char c : manifest_name)
                if (c >= '0' && c <= '9')
                    n = n * 10 + (uint64_t)(c - '0');
            manifest_number_ = n;
        }
        for (const FileMeta& f : files_)
            max_file_number = (f.number > max_file_number) ? f.number : max_file_number;
    }

    // Keep only tables that actually exist on disk (a crash can orphan a
    // manifest entry; the table's data rides the WAL of the same era).
    files_.erase(std::remove_if(files_.begin(), files_.end(),
                                [&](const FileMeta& f) {
                                    struct stat st{};
                                    return ::stat(table_path(f.number).c_str(), &st) != 0;
                                }),
                 files_.end());

    // Replay every WAL from log_number_ upward (ascending). A crash between
    // "table written" and "manifest rewritten" leaves both old and new logs;
    // replaying in order keeps the last write of every key.
    uint64_t max_seq = last_sequence_;
    if (!replay_logs_locked(log_number_, &max_seq, err))
        return fail_locked(err ? *err : "WAL replay failed");
    last_sequence_ = max_seq;

    // Highest file number on disk (including logs/manifests) bounds the
    // numbering space.
    DIR* d = ::opendir(dir_.c_str());
    if (d) {
        struct dirent* ent;
        while ((ent = ::readdir(d)) != nullptr) {
            const char* name = ent->d_name;
            if (name[0] < '0' || name[0] > '9')
                continue;
            uint64_t n = 0;
            for (const char* p = name; *p >= '0' && *p <= '9'; ++p)
                n = n * 10 + (uint64_t)(*p - '0');
            if (n > max_file_number)
                max_file_number = n;
        }
        ::closedir(d);
    }
    next_file_number_ = (max_file_number > 0) ? max_file_number + 1 : 1;

    // Data replayed from the WAL goes straight into a table so the next
    // open starts clean (the old logs are then removed by the manifest
    // swap below; replay never re-runs on old data).
    if (!memtable_.empty() && !flush_locked(err))
        return fail_locked(err ? *err : "flush after recovery failed");

    // Roll to a fresh WAL even when nothing was replayed: the manifest we
    // write below pins THIS log, and every older one becomes garbage.
    if (log_file_) {
        std::fclose(log_file_);
        log_file_ = nullptr;
    }
    log_number_ = next_file_number_++;
    log_file_ = std::fopen(log_path(log_number_).c_str(), "wb");
    wal_bytes_ = 0;
    if (!log_file_)
        return fail_locked("cannot create WAL " + log_path(log_number_));
    if (!write_manifest_locked(err))
        return fail_locked(err ? *err : "manifest write failed");

    // Remove WALs the new manifest no longer references (everything below
    // log_number_).
    d = ::opendir(dir_.c_str());
    if (d) {
        struct dirent* ent;
        while ((ent = ::readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name.size() < 5 || name.compare(name.size() - 4, 4, ".log") != 0)
                continue;
            uint64_t n = 0;
            bool digits = true;
            for (char c : name) {
                if (c == '.')
                    break;
                if (c < '0' || c > '9') {
                    digits = false;
                    break;
                }
                n = n * 10 + (uint64_t)(c - '0');
            }
            if (digits && n < log_number_)
                std::remove((dir_ + "/" + name).c_str());
        }
        ::closedir(d);
    }

    opened_ = true;
    return true;
}

bool DBEngine::replay_logs_locked(uint64_t from_number, uint64_t* max_seq,
                                  std::string* err) {
    // WALs present on disk with number >= from_number, ascending.
    std::vector<uint64_t> numbers;
    DIR* d = ::opendir(dir_.c_str());
    if (!d)
        return true;
    struct dirent* ent;
    while ((ent = ::readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() < 5 || name.compare(name.size() - 4, 4, ".log") != 0)
            continue;
        uint64_t n = 0;
        bool digits = true;
        for (char c : name) {
            if (c == '.')
                break;
            if (c < '0' || c > '9') {
                digits = false;
                break;
            }
            n = n * 10 + (uint64_t)(c - '0');
        }
        if (digits && n >= from_number)
            numbers.push_back(n);
    }
    ::closedir(d);
    std::sort(numbers.begin(), numbers.end());

    for (uint64_t n : numbers) {
        std::vector<uint8_t> image;
        if (!read_file_bytes(log_path(n), &image))
            continue; // unreadable WAL: its data is also in the tables
        db::LogReader reader(image);
        const uint8_t* payload = nullptr;
        size_t plen = 0;
        while (reader.next(&payload, &plen)) {
            uint64_t seq = 0;
            std::vector<std::pair<std::string, std::optional<std::string>>> ops;
            if (!decode_batch(payload, plen, &seq, &ops))
                break; // torn tail of a crashed write: stop here
            uint64_t s = seq;
            for (const auto& [key, value] : ops) {
                uint8_t type = value.has_value() ? db::kTypeValue : db::kTypeDeletion;
                std::string internal = db::make_internal_key(key, s, type);
                memtable_[std::move(internal)] = value.value_or("");
                memtable_bytes_ += key.size() + (value ? value->size() : 0) + 48;
                ++s;
            }
            if (s > *max_seq)
                *max_seq = s - 1;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Writes
// ---------------------------------------------------------------------------

bool DBEngine::put_batch(
    const std::vector<std::pair<std::string, std::optional<std::string>>>& ops) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!opened_ || ops.empty())
        return opened_;

    uint64_t seq = last_sequence_ + 1;
    std::vector<uint8_t> record;
    encode_batch(seq, ops, &record);

    // WAL first (the durability point), then the memtable. The batch rides
    // a real log record (header + CRC + 32 KiB block discipline), framed at
    // the WAL's current byte offset so multi-record files stay aligned.
    std::vector<uint8_t> framed;
    db::log_append_record(&framed, record.data(), record.size(), wal_bytes_);
    if (std::fwrite(framed.data(), 1, framed.size(), log_file_) != framed.size() ||
        std::fflush(log_file_) != 0)
        return false;
    wal_bytes_ += framed.size();
    performance::SaveProfiler::noteWalBytes(framed.size());

    for (const auto& [key, value] : ops) {
        uint8_t type = value.has_value() ? db::kTypeValue : db::kTypeDeletion;
        std::string internal = db::make_internal_key(key, seq, type);
        memtable_[std::move(internal)] = value.value_or("");
        memtable_bytes_ += key.size() + (value ? value->size() : 0) + 48;
    }
    last_sequence_ = seq;
    ++memtable_batch_count_;

    // Real-leveldb write-buffer behavior: cross the memtable threshold and
    // the engine flushes it into a table right here (the caller never has
    // to care). The batch itself is already durable in the WAL.
    if (memtable_bytes_ >= kMemtableFlushBytes) {
        std::string e;
        flush_locked(&e);
    }
    return true;
}

std::optional<std::string> DBEngine::get(const std::string& user_key) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!opened_)
        return std::nullopt;

    // 1) Memtable (newest data).
    std::string target =
        db::make_internal_key(user_key, db::kMaxSequenceNumber, db::kTypeValue);
    auto it = memtable_.lower_bound(target);
    if (it != memtable_.end() && db::user_key_of(it->first) == user_key) {
        if (db::type_of(it->first) == db::kTypeValue)
            return it->second;
        return std::nullopt; // newest version is a tombstone
    }

    // 2) Tables: level 0 by descending file number (newest wins), then
    //    levels 1..7 ascending — the LSM lookup order.
    std::vector<const FileMeta*> level0;
    for (const FileMeta& f : files_) {
        if (f.level == 0)
            level0.push_back(&f);
    }
    std::sort(level0.begin(), level0.end(),
              [](const FileMeta* a, const FileMeta* b) { return a->number > b->number; });

    db::TableEntry entry;
    for (const FileMeta* f : level0) {
        std::vector<uint8_t> image;
        if (!read_file_bytes(table_path(f->number), &image))
            continue;
        db::TableReader reader;
        if (!reader.open(image.data(), image.size()))
            continue;
        if (reader.get(target, &entry))
            return (db::type_of(entry.key) == db::kTypeValue)
                       ? std::optional<std::string>(entry.value)
                       : std::nullopt;
    }
    for (int level = 1; level < 8; ++level) {
        for (const FileMeta& f : files_) {
            if (f.level != level)
                continue;
            // Range reject on USER keys only: the internal-key comparator
            // orders same-user keys newest-first, so seq-carrying bounds
            // must never be compared against a seek key here.
            const std::string fmin = db::user_key_of(f.smallest);
            const std::string fmax = db::user_key_of(f.largest);
            if (user_key < fmin || fmax < user_key)
                continue;
            std::vector<uint8_t> image;
            if (!read_file_bytes(table_path(f.number), &image))
                continue;
            db::TableReader reader;
            if (!reader.open(image.data(), image.size()))
                continue;
            if (reader.get(target, &entry))
                return (db::type_of(entry.key) == db::kTypeValue)
                           ? std::optional<std::string>(entry.value)
                           : std::nullopt;
        }
    }
    return std::nullopt;
}

void DBEngine::scan(
    const std::string& prefix,
    const std::function<void(const std::string&, const std::string&)>& sink) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!opened_)
        return;

    // Merge every source newest-first: memtable, then level-0 by file
    // number descending, then higher levels. For a given user key the
    // first source that carries it wins; tombstones silence it. Sequences
    // are globally monotonic, so ordering rows by the internal-key
    // comparator (user asc, seq desc) puts the winning row of every user
    // key first in its group.
    struct Row {
        std::string user;
        std::string value;
        uint8_t type;
    };
    std::vector<Row> rows;
    for (const auto& [key, value] : memtable_) {
        std::string user = db::user_key_of(key);
        if (user.compare(0, prefix.size(), prefix) == 0)
            rows.push_back({std::move(user), value, db::type_of(key)});
    }
    auto read_table_rows = [&](const FileMeta& f) {
        std::vector<uint8_t> image;
        if (!read_file_bytes(table_path(f.number), &image))
            return;
        db::TableReader reader;
        if (!reader.open(image.data(), image.size()))
            return;
        std::vector<TableEntry> entries;
        reader.scan(&entries);
        for (TableEntry& e : entries) {
            std::string user = db::user_key_of(e.key);
            if (user.compare(0, prefix.size(), prefix) == 0)
                rows.push_back({std::move(user), std::move(e.value),
                                db::type_of(e.key)});
        }
        (void)f.level;
    };
    std::vector<const FileMeta*> level0;
    for (const FileMeta& f : files_) {
        if (f.level == 0)
            level0.push_back(&f);
    }
    std::sort(level0.begin(), level0.end(),
              [](const FileMeta* a, const FileMeta* b) { return a->number > b->number; });
    for (const FileMeta* f : level0)
        read_table_rows(*f);
    for (const FileMeta& f : files_) {
        if (f.level != 0)
            read_table_rows(f);
    }

    // Order: user key ascending, then newest first. The first row of each
    // user key decides its fate.
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        int cmp = a.user.compare(b.user);
        return cmp < 0;
    });
    const std::string* last_user = nullptr;
    for (const Row& r : rows) {
        if (last_user && r.user == *last_user)
            continue;
        last_user = &r.user;
        if (r.type == db::kTypeValue)
            sink(r.user, r.value);
    }
}

// ---------------------------------------------------------------------------
// Flush + compaction
// ---------------------------------------------------------------------------

bool DBEngine::flush(std::string* err) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!opened_)
        return false;
    return flush_locked(err);
}

bool DBEngine::flush_locked(std::string* err) {
    if (memtable_.empty()) {
        return true;
    }
    // The whole memtable -> table transition (SSTable build + write +
    // fsync + WAL rotation + manifest rewrite) is timed as EngineFlush.
    // This runs on the save worker inside a put_batch when the memtable
    // crosses its threshold, and from flush() at full-save points.
    performance::SaveProfiler::Scope flush_probe(performance::SaveStage::EngineFlush);
    performance::SaveProfiler::noteFlush();
    // Snapshot the memtable into table entries.
    std::vector<TableEntry> entries;
    entries.reserve(memtable_.size());
    for (const auto& [key, value] : memtable_)
        entries.push_back({key, value});

    uint64_t number = next_file_number_++;
    std::vector<uint8_t> image;
    if (!db::build_table(entries, &image)) {
        --next_file_number_; // give the number back
        if (err)
            *err = "failed building table";
        return false;
    }
    if (!write_file_bytes(table_path(number), image.data(), image.size(),
                          /*sync=*/true)) {
        --next_file_number_;
        if (err)
            *err = "failed writing table " + table_path(number);
        return false;
    }
    performance::SaveProfiler::noteTableBytes(image.size());

    FileMeta meta;
    meta.level = 0;
    meta.number = number;
    meta.size = image.size();
    meta.smallest = entries.front().key;
    meta.largest = entries.back().key;
    files_.push_back(std::move(meta));

    // Rotate the WAL: everything before this point is durable in the table.
    memtable_.clear();
    memtable_bytes_ = 0;
    memtable_batch_count_ = 0;
    if (log_file_) {
        std::fclose(log_file_);
        log_file_ = nullptr;
    }
    std::remove(log_path(log_number_).c_str());
    log_number_ = next_file_number_++;
    log_file_ = std::fopen(log_path(log_number_).c_str(), "wb");
    wal_bytes_ = 0;
    if (!log_file_) {
        if (err)
            *err = "cannot create WAL after flush";
        return false;
    }

    if (!write_manifest_locked(err))
        return false;

    // Bound the table set: merge everything once level 0 piles up.
    size_t level0_count = 0;
    for (const FileMeta& f : files_) {
        if (f.level == 0)
            ++level0_count;
    }
    if (level0_count >= kLevel0CompactionTrigger)
        return compact_locked(err);
    return true;
}

bool DBEngine::compact_locked(std::string* err) {
    if (files_.empty())
        return true;
    // Full merge: read every table, dedupe, write the merged one. This is
    // the O(entire-database) I/O burst the engine runs inline once level-0
    // piles up — timed as EngineCompact so a slow-disk save window can be
    // attributed precisely instead of hiding inside WorkerPut.
    performance::SaveProfiler::Scope compact_probe(performance::SaveStage::EngineCompact);
    performance::SaveProfiler::noteCompact();
    // Read every table, newest source first, and keep only the newest
    // version of each user key (no snapshots exist → older versions and
    // tombstones can drop entirely).
    uint64_t compact_read_bytes = 0;
    std::vector<const FileMeta*> level0;
    for (const FileMeta& f : files_) {
        if (f.level == 0)
            level0.push_back(&f);
    }
    std::sort(level0.begin(), level0.end(),
              [](const FileMeta* a, const FileMeta* b) { return a->number > b->number; });

    struct Row {
        std::string key;
        std::string value;
        uint8_t type;
    };
    std::vector<Row> rows;
    for (const FileMeta* f : level0) {
        std::vector<uint8_t> image;
        if (!read_file_bytes(table_path(f->number), &image))
            continue;
        compact_read_bytes += image.size();
        db::TableReader reader;
        if (!reader.open(image.data(), image.size()))
            continue;
        std::vector<TableEntry> entries;
        reader.scan(&entries);
        for (TableEntry& e : entries) {
            uint8_t t = db::type_of(e.key); // read BEFORE the move below
            rows.push_back({std::move(e.key), std::move(e.value), t});
        }
    }
    for (const FileMeta& f : files_) {
        if (f.level == 0)
            continue;
        std::vector<uint8_t> image;
        if (!read_file_bytes(table_path(f.number), &image))
            continue;
        compact_read_bytes += image.size();
        db::TableReader reader;
        if (!reader.open(image.data(), image.size()))
            continue;
        std::vector<TableEntry> entries;
        reader.scan(&entries);
        for (TableEntry& e : entries) {
            uint8_t t = db::type_of(e.key); // read BEFORE the move below
            rows.push_back({std::move(e.key), std::move(e.value), t});
        }
    }

    // Internal-key order (user asc, seq desc): the first row of every user
    // group is the newest version. Sequences are globally unique, so
    // "newest" is unambiguous across sources and no tie-break is needed.
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return db::internal_key_less(a.key, b.key);
    });
    std::vector<TableEntry> merged;
    {
        size_t i = 0;
        while (i < rows.size()) {
            const std::string user = db::user_key_of(rows[i].key);
            if (rows[i].type == db::kTypeValue) {
                merged.push_back({std::move(rows[i].key), std::move(rows[i].value)});
            }
            size_t j = i + 1;
            while (j < rows.size() && db::user_key_of(rows[j].key) == user)
                ++j;
            i = j;
        }
    }
    // merged is already in internal-key order (first-of-group walk); the
    // table builder just needs consistency, which the walk preserves.
    if (merged.empty()) {
        // Everything tombstoned: just drop the tables.
        for (const FileMeta& f : files_)
            std::remove(table_path(f.number).c_str());
        files_.clear();
        return write_manifest_locked(err);
    }

    uint64_t number = next_file_number_++;
    std::vector<uint8_t> image;
    if (!db::build_table(merged, &image)) {
        --next_file_number_;
        if (err)
            *err = "failed building compaction table";
        return false;
    }
    if (!write_file_bytes(table_path(number), image.data(), image.size(),
                          /*sync=*/true)) {
        --next_file_number_;
        if (err)
            *err = "failed writing compaction table " + table_path(number);
        return false;
    }
    performance::SaveProfiler::noteTableBytes(image.size());
    performance::SaveProfiler::noteCompactBytes(compact_read_bytes);

    for (const FileMeta& f : files_)
        std::remove(table_path(f.number).c_str());
    files_.clear();
    FileMeta meta;
    meta.level = 1;
    meta.number = number;
    meta.size = image.size();
    meta.smallest = merged.front().key;
    meta.largest = merged.back().key;
    files_.push_back(std::move(meta));
    return write_manifest_locked(err);
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------

void DBEngine::close() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!opened_ && !log_file_ && lock_fd_ < 0)
        return;
    if (opened_ && !memtable_.empty()) {
        std::string e;
        flush_locked(&e);
    }
    if (log_file_) {
        std::fclose(log_file_);
        log_file_ = nullptr;
    }
    if (lock_fd_ >= 0) {
        db_unlock(lock_fd_);
        db_close_fd(lock_fd_);
        lock_fd_ = -1;
    }
    opened_ = false;
}

} // namespace cursestone::world::format::leveldb

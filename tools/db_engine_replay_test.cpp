// Standalone DBEngine test: batches -> "crash" (no close) -> reopen -> get.
// Build: g++ -std=c++20 -Isrc -o /tmp/dbtest tools/db_engine_replay_test.cpp \
//          src/world/format/leveldb/DBFormat.cpp src/world/format/leveldb/DBTable.cpp \
//          src/world/format/leveldb/DBEngine.cpp src/utils/BinaryStream.c -lz -lpthread
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "world/format/leveldb/DBEngine.h"
#include "world/format/leveldb/DBTable.h"

using namespace cursestone::world::format::leveldb;

static std::string big_value(int seed) {
    std::string v(83200, '\0');
    for (size_t i = 0; i < v.size(); i += 4096)
        v[i] = (char)(seed & 0xFF);
    return v;
}

static bool put_chunk(DBEngine& db, int cx, int cz, int gen, uint8_t record) {
    std::vector<uint8_t> key(9);
    uint32_t ux = (uint32_t)cx, uz = (uint32_t)cz;
    key[0] = (uint8_t)ux; key[1] = (uint8_t)(ux >> 8);
    key[2] = (uint8_t)(ux >> 16); key[3] = (uint8_t)(ux >> 24);
    key[4] = (uint8_t)uz; key[5] = (uint8_t)(uz >> 8);
    key[6] = (uint8_t)(uz >> 16); key[7] = (uint8_t)(uz >> 24);
    key[8] = record;
    std::vector<std::pair<std::string, std::optional<std::string>>> ops;
    ops.emplace_back(std::string(key.begin(), key.end()), big_value(gen));
    return db.put_batch(ops);
}

int main(int argc, char** argv) {
    std::string dir = "/tmp/dbengine_test";
    std::string mode = (argc > 1) ? argv[1] : "all";

    if (mode == "write") {
        // Session 1: open, write 3 batches, then die WITHOUT close/destructor
        // — _exit() skips both, exactly like SIGKILL.
        DBEngine db;
        std::string err;
        if (!db.open(dir, &err)) {
            fprintf(stderr, "open1 failed: %s\n", err.c_str());
            return 1;
        }
        for (int i = 0; i < 3; ++i)
            if (!put_chunk(db, 8, 8, 0x10 + i, 0x30)) {
                fprintf(stderr, "put %d failed\n", i);
                return 1;
            }
        fprintf(stderr, "session 1: 3 batches written, dying hard\n");
        ::_Exit(0); // SIGKILL semantics: no flush, no close, no dtor
    }

    if (mode == "verify") {
        // Session 2: reopen; the LAST write (gen 0x12) must be visible.
        DBEngine db;
        std::string err;
        if (!db.open(dir, &err)) {
            fprintf(stderr, "open2 failed: %s\n", err.c_str());
            return 1;
        }
        std::vector<uint8_t> key(9);
        key[0] = 8;
        key[4] = 8;
        key[8] = 0x30;
        std::string user_key(key.begin(), key.end());
        auto v = db.get(user_key);
        if (!v) {
            fprintf(stderr, "FAIL: key lost across crash\n");
            // Diagnostics: what did the recovery flush leave on disk?
            for (auto& e : std::filesystem::directory_iterator(dir))
                fprintf(stderr, "  file: %s (%zu bytes)\n", e.path().string().c_str(),
                        (size_t)std::filesystem::file_size(e.path()));
            // Direct TableReader probe of every .ldb on disk.
            for (auto& e : std::filesystem::directory_iterator(dir)) {
                std::string p = e.path().string();
                if (p.size() < 4 || p.compare(p.size() - 4, 4, ".ldb") != 0)
                    continue;
                FILE* fp = fopen(p.c_str(), "rb");
                if (!fp) continue;
                std::vector<uint8_t> img;
                uint8_t buf[65536];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
                    img.insert(img.end(), buf, buf + n);
                fclose(fp);
                db::TableReader reader;
                bool ok = reader.open(img.data(), img.size());
                fprintf(stderr, "  table %s: open=%s blocks=%zu\n", p.c_str(),
                        ok ? "OK" : "FAIL", ok ? reader.block_count() : 0);
                if (ok) {
                    db::TableEntry out;
                    std::vector<uint8_t> key(9);
                    key[0] = 8; key[4] = 8; key[8] = 0x30;
                    std::string user(key.begin(), key.end());
                    std::string tgt = db::make_internal_key(
                        user, db::kMaxSequenceNumber, db::kTypeValue);
                    bool hit = reader.get(tgt, &out);
                    fprintf(stderr, "  direct get: %s (type=%d vlen=%zu)\n",
                            hit ? "HIT" : "MISS",
                            hit ? (int)db::type_of(out.key) : -1,
                            hit ? out.value.size() : 0);
                }
            }
            return 1;
        }
        char expect = (char)0x12;
        bool ok = v->size() == 83200 && (*v)[0] == expect;
        printf("%s: reopened value first byte = 0x%02x (want 0x12), size = %zu\n",
               ok ? "PASS" : "FAIL", (unsigned char)(*v)[0], v->size());
        // write one more batch, flush, close cleanly
        put_chunk(db, 8, 8, 0x20, 0x30);
        db.flush();
        db.close();
        return ok ? 0 : 1;
    }

    // "all": run the crash phase in a child process, then verify here.
    std::filesystem::remove_all(dir);
    {
        std::string self = argv[0];
        std::string cmd = self + " write";
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            fprintf(stderr, "write phase failed (rc=%d)\n", rc);
            return 1;
        }
    }
    std::string cmd2 = std::string(argv[0]) + " verify";
    if (std::system(cmd2.c_str()) != 0)
        return 1;

    // ---- overwrite / tombstone / compaction --------------------------------
    {
        DBEngine db;
        std::string err;
        if (!db.open(dir + "_2", &err)) {
            fprintf(stderr, "open3 failed: %s\n", err.c_str());
            return 1;
        }
        auto put1 = [&](const char* k, const char* v) {
            std::vector<std::pair<std::string, std::optional<std::string>>> ops;
            if (v)
                ops.emplace_back(k, std::string(v));
            else
                ops.emplace_back(k, std::nullopt);
            return db.put_batch(ops);
        };
        put1("alpha", "one");
        put1("alpha", "two"); // overwrite
        put1("beta", "bee");
        put1("alpha", nullptr); // tombstone
        // 9 flushes -> 8+ level-0 tables -> full compaction must kick in.
        for (int i = 0; i < 9; ++i) {
            char k[16], v[16];
            snprintf(k, sizeof(k), "chunk-%02d", i);
            snprintf(v, sizeof(v), "gen-%02d", i);
            put1(k, v);
            db.flush();
        }
        auto a = db.get("alpha");
        auto b = db.get("beta");
        bool ok = !a.has_value() && b.has_value() && *b == "bee";
        for (int i = 0; i < 9; ++i) {
            char k[16], v[16], want[16];
            snprintf(k, sizeof(k), "chunk-%02d", i);
            snprintf(v, sizeof(v), "gen-%02d", i);
            auto got = db.get(k);
            if (!got || *got != v) {
                ok = false;
                fprintf(stderr, "  chunk-%02d lost after compaction\n", i);
            }
        }
        db.close();
        printf("%s: overwrite+tombstone+compaction\n", ok ? "PASS" : "FAIL");
        if (!ok)
            return 1;
    }
    return 0;
}

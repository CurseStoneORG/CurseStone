// SPDX-License-Identifier: LGPL-3.0-or-later
// McaCpp — implementation of the default CenisysPE world format.
//
// Disk layout (see docs/MCACPP_FORMAT.md):
//   <world>/chunks/<rx>_<rz>.mcacpp
//     [0..256)   256-byte header (magic, version, rx, rz, ts, reserved)
//     [256..4352)  4096-byte chunk table (1024 entries × 4 bytes)
//     [4352..)   variable-size chunk payloads
//
// Each chunk table entry:
//   bits 24..31  SectorCount (8-bit, max 255 → 1 MiB per chunk max)
//   bits 0..23   SectorOffset (24-bit, max 16 Mi sectors → 64 GiB)
// Sectors are 4096 bytes. A zero SectorOffset means "not stored".
//
// Chunk payload:
//   4 bytes BE  CompressedLength (excludes these 4 bytes + comp byte)
//   1 byte     CompressionType (0=none, 1=zlib, 2=lz4)
//   N bytes     Compressed NBT (CompoundTag "Level")
//
// For Phase 8 we ship the full region-file machinery (header parse,
// chunk table walk, sector alloc, zlib compress/decompress on raw
// payloads) plus stub `loadChunk`/`saveChunk` that defer the
// mcp_chunk_t <-> NBT codec to Phase 9 (ChunkNBTCodec).
#include "McaCpp.h"
#include "../LevelProviderManager.h"
#include "../ChunkAdapter.h"
#include "../ChunkNBTCodec.h"
#include "../../../nbt/NBT.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>

namespace cenisyspe::level::format::mcacpp {

// ----- Auto-registration with the LevelProviderManager.
McaCppRegistrar g_mcacpp_registrar_;
McaCppRegistrar::McaCppRegistrar() {
    LevelProviderManager::getInstance().registerProvider(
        "mcacpp", [](){ return std::unique_ptr<LevelProvider>(new McaCpp()); });
}

// ----- Magic + version constants.
static constexpr uint32_t kMagic     = 0x4D434143u;   // "MCAC"
static constexpr uint8_t  kVersion   = 0x01u;
static constexpr size_t   kHeaderSize   = 256;
static constexpr size_t   kTableSize    = 4096;
static constexpr size_t   kTableEntries = 1024;
static constexpr size_t   kSectorSize   = 4096;
static constexpr size_t   kMaxPayloadSize = 1024 * 1024;  // hard cap

// ----- Big-endian helpers (header is BE per spec).
static void write_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)(v);
}
static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static void write_be64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (56 - 8 * i));
}
// (read_be64 not currently used — Phase 9 metadata reader will.)

// Encode a chunk table entry: offset (24-bit) + sectorCount (8-bit)
// into a 32-bit word that we store in network byte order so the file
// is portable across architectures.
static void write_table_entry(uint8_t* p, uint32_t offset, uint8_t count) {
    uint32_t v = (offset & 0x00FFFFFF) | ((uint32_t)count << 24);
    write_be32(p, v);
}
static void read_table_entry(const uint8_t* p, uint32_t* off, uint8_t* count) {
    uint32_t v = read_be32(p);
    *off   = v & 0x00FFFFFF;
    *count = (uint8_t)(v >> 24);
}

// ----- zlib helpers.
static bool zlib_compress(const uint8_t* src, size_t src_len,
                            std::vector<uint8_t>* out) {
    uLongf bound = compressBound((uLong)src_len);
    out->resize(bound);
    uLongf dest_len = bound;
    int rc = compress2(out->data(), &dest_len, src, (uLong)src_len, 7);
    if (rc != Z_OK) {
        out->clear();
        return false;
    }
    out->resize(dest_len);
    return true;
}

// ----- Directory helpers (POSIX).
static bool ensure_dir(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (errno != ENOENT) return false;
    return ::mkdir(path.c_str(), 0775) == 0;
}

// ----- McaCpp lifecycle.

McaCpp::McaCpp() = default;

McaCpp::~McaCpp() {
    shutdown();
}

void McaCpp::init(const std::string& path) {
    path_ = path;
    // Create chunks/ subdirectory.
    ensure_dir(path_);
    ensure_dir(path_ + "/chunks");
    loadMetadata();
}

void McaCpp::shutdown() {
    // Flush + close every open region.
    for (auto& [_, rf_ptr] : regions_) {
        if (rf_ptr && rf_ptr->fp) {
            std::fflush(rf_ptr->fp);
            std::fclose(rf_ptr->fp);
            rf_ptr->fp = nullptr;
        }
    }
    regions_.clear();
    saveMetadata();
}

// ----- Region path / cache.

std::string McaCpp::regionPath(int32_t cx, int32_t cz) const {
    int32_t rx = (cx >= 0) ? (cx / 32) : ((cx - 31) / 32);
    int32_t rz = (cz >= 0) ? (cz / 32) : ((cz - 31) / 32);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s/chunks/%d_%d.mcacpp",
                   path_.c_str(), (int)rx, (int)rz);
    return std::string(buf);
}

RegionFile* McaCpp::regionFor(int32_t cx, int32_t cz, bool create) {
    std::string p = regionPath(cx, cz);
    auto it = regions_.find(p);
    if (it != regions_.end()) return it->second.get();

    if (regions_.size() >= kMaxOpenRegions_) {
        evictOldestRegion();
    }

    // Try to open existing file. If absent and `create` is true,
    // create + initialize header + table.
    FILE* fp = std::fopen(p.c_str(), "rb+");
    if (!fp) {
        if (!create) return nullptr;
        fp = std::fopen(p.c_str(), "wb+");
        if (!fp) return nullptr;
        // Write empty header + table (8 KiB of zeros).
        std::vector<uint8_t> zero(kHeaderSize + kTableSize, 0);
        // Patch in magic + version + rx + rz + ts.
        uint32_t rx = (cx >= 0) ? (uint32_t)(cx / 32)
                                : (uint32_t)((cx - 31) / 32);
        uint32_t rz = (cz >= 0) ? (uint32_t)(cz / 32)
                                : (uint32_t)((cz - 31) / 32);
        write_be32(zero.data() + 0, kMagic);
        zero[4] = kVersion;
        write_be32(zero.data() + 5, rx);
        write_be32(zero.data() + 9, rz);
        auto now = std::chrono::system_clock::now().time_since_epoch();
        write_be64(zero.data() + 13,
                    (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        if (std::fwrite(zero.data(), 1, zero.size(), fp) != zero.size()) {
            std::fclose(fp); std::remove(p.c_str()); return nullptr;
        }
        std::fflush(fp);
        std::fseek(fp, 0, SEEK_SET);

        auto rf = std::make_unique<RegionFile>();
        rf->path      = p;
        rf->fp        = fp;
        rf->region_x  = (int32_t)rx;
        rf->region_z  = (int32_t)rz;
        rf->writable   = true;
        std::fill(rf->table.begin(), rf->table.end(), 0u);
        regions_[p] = std::move(rf);
        return regions_[p].get();
    }

    // Existing file: read header + table.
    auto rf = std::make_unique<RegionFile>();
    rf->path     = p;
    rf->fp       = fp;
    rf->writable = true;
    int32_t rx = 0, rz = 0;
    std::array<uint32_t, 1024> table{};
    if (!readRegionHeader(fp, &rx, &rz, &table)) {
        std::fclose(fp);
        return nullptr;
    }
    rf->region_x = rx;
    rf->region_z = rz;
    rf->table    = table;
    regions_[p] = std::move(rf);
    return regions_[p].get();
}

bool McaCpp::readRegionHeader(FILE* fp, int32_t* out_rx,
                                  int32_t* out_rz,
                                  std::array<uint32_t, 1024>* out_table) {
    if (!fp || !out_rx || !out_rz || !out_table) return false;
    uint8_t hdr[kHeaderSize];
    if (std::fseek(fp, 0, SEEK_SET) != 0) return false;
    if (std::fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) return false;
    if (read_be32(hdr + 0) != kMagic) return false;
    if (hdr[4] != kVersion) return false;
    *out_rx = (int32_t)read_be32(hdr + 5);
    *out_rz = (int32_t)read_be32(hdr + 9);
    // Skip 8-byte ts + 235 reserved — already past via fread.
    uint8_t tbl[kTableSize];
    if (std::fread(tbl, 1, sizeof(tbl), fp) != sizeof(tbl)) return false;
    for (size_t i = 0; i < kTableEntries; ++i) {
        uint32_t off; uint8_t cnt;
        read_table_entry(tbl + i * 4, &off, &cnt);
        (*out_table)[i] = (off & 0x00FFFFFF) | ((uint32_t)cnt << 24);
    }
    return true;
}

bool McaCpp::writeRegionTable(FILE* fp, int32_t rx, int32_t rz,
                                  const std::array<uint32_t, 1024>& table) {
    if (!fp) return false;
    uint8_t tbl[kTableSize];
    for (size_t i = 0; i < kTableEntries; ++i) {
        uint32_t v = table[i];
        uint32_t off = v & 0x00FFFFFF;
        uint8_t  cnt = (uint8_t)(v >> 24);
        write_table_entry(tbl + i * 4, off, cnt);
    }
    if (std::fseek(fp, 0, SEEK_SET) != 0) return false;
    // Rewrite magic + version + rx + rz + ts + reserved (256 bytes)
    // then the table (4096 bytes).
    uint8_t hdr[kHeaderSize] = {0};
    write_be32(hdr + 0, kMagic);
    hdr[4] = kVersion;
    write_be32(hdr + 5, (uint32_t)rx);
    write_be32(hdr + 9, (uint32_t)rz);
    auto now = std::chrono::system_clock::now().time_since_epoch();
    write_be64(hdr + 13,
                (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    if (std::fwrite(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) return false;
    if (std::fwrite(tbl, 1, sizeof(tbl), fp) != sizeof(tbl)) return false;
    std::fflush(fp);
    return true;
}

// ----- Payload read / write.

std::optional<std::vector<uint8_t>>
McaCpp::readChunkPayload(int32_t cx, int32_t cz) {
    RegionFile* rf = regionFor(cx, cz, /*create=*/false);
    if (!rf || !rf->fp) return std::nullopt;
    int local_x = cx & 31;
    int local_z = cz & 31;
    uint32_t v = rf->table[RegionFile::tableIndex(local_x, local_z)];
    uint32_t off = v & 0x00FFFFFF;
    uint8_t  cnt = (uint8_t)(v >> 24);
    if (off == 0 || cnt == 0) return std::nullopt;

    long byte_off = (long)off * (long)kSectorSize;
    if (std::fseek(rf->fp, byte_off, SEEK_SET) != 0) return std::nullopt;
    uint8_t hdr[5];
    if (std::fread(hdr, 1, sizeof(hdr), rf->fp) != sizeof(hdr))
        return std::nullopt;
    uint32_t comp_len = read_be32(hdr + 0);
    uint8_t  comp     = hdr[4];
    if (comp_len == 0 || comp_len > kMaxPayloadSize) return std::nullopt;
    std::vector<uint8_t> comp_buf(comp_len);
    if (std::fread(comp_buf.data(), 1, comp_len, rf->fp) != comp_len)
        return std::nullopt;

    if (comp == 0) {
        return comp_buf;   // uncompressed NBT
    } else if (comp == 1) {
        // zlib-compressed; expected uncompressed size is unknown
        // ahead of time, so use streaming inflate.
        z_stream s{};
        s.next_in   = (Bytef*)comp_buf.data();
        s.avail_in  = (uInt)comp_buf.size();
        if (inflateInit(&s) != Z_OK) return std::nullopt;
        std::vector<uint8_t> out;
        out.resize(comp_buf.size() * 8);   // initial guess
        s.next_out  = out.data();
        s.avail_out = (uInt)out.size();
        int rc = Z_OK;
        while ((rc = inflate(&s, Z_NO_FLUSH)) == Z_OK && s.avail_out == 0) {
            size_t old = out.size();
            out.resize(old * 2);
            s.next_out  = out.data() + old;
            s.avail_out = (uInt)(out.size() - old);
        }
        inflateEnd(&s);
        if (rc != Z_STREAM_END) return std::nullopt;
        out.resize(s.total_out);
        return out;
    }
    // LZ4 (comp == 2) not yet supported.
    return std::nullopt;
}

uint32_t McaCpp::allocSectors(RegionFile& rf, uint8_t sectorCount) {
    // Phase 8 strategy: just append. We track the highest sector
    // in use across the table, and allocate past it.
    uint32_t max_end = (uint32_t)((kHeaderSize + kTableSize + kSectorSize - 1)
                                    / kSectorSize);
    for (uint32_t v : rf.table) {
        uint32_t off = v & 0x00FFFFFF;
        uint8_t  cnt = (uint8_t)(v >> 24);
        if (off == 0) continue;
        uint32_t end = off + cnt;
        if (end > max_end) max_end = end;
    }
    // Grow the file to (max_end + sectorCount) sectors.
    long new_size = (long)(max_end + sectorCount) * (long)kSectorSize;
    if (std::fseek(rf.fp, new_size - 1, SEEK_SET) != 0) return 0;
    uint8_t z = 0;
    if (std::fwrite(&z, 1, 1, rf.fp) != 1) return 0;
    std::fflush(rf.fp);
    return max_end;
}

bool McaCpp::writeChunkPayload(int32_t cx, int32_t cz,
                                  uint8_t compression,
                                  const std::vector<uint8_t>& payload) {
    if (compression > 2) return false;
    RegionFile* rf = regionFor(cx, cz, /*create=*/true);
    if (!rf || !rf->fp) return false;

    std::vector<uint8_t> comp_buf;
    if (compression == 1) {
        if (!zlib_compress(payload.data(), payload.size(), &comp_buf))
            return false;
    } else if (compression == 2) {
        // LZ4 not supported in Phase 8.
        return false;
    } else {
        comp_buf = payload;
    }

    // 4-byte len + 1-byte comp + comp_buf; padded to kSectorSize.
    size_t needed = 4 + 1 + comp_buf.size();
    if (needed == 0) return false;
    size_t sector_count_sz = (needed + kSectorSize - 1) / kSectorSize;
    if (sector_count_sz == 0 || sector_count_sz > 255) return false;  // chunk too large
    uint8_t sector_count = (uint8_t)sector_count_sz;

    uint32_t off = allocSectors(*rf, sector_count);
    if (off == 0) return false;
    long byte_off = (long)off * (long)kSectorSize;
    if (std::fseek(rf->fp, byte_off, SEEK_SET) != 0) return false;
    uint8_t hdr[5];
    write_be32(hdr + 0, (uint32_t)comp_buf.size());
    hdr[4] = compression;
    if (std::fwrite(hdr, 1, 5, rf->fp) != 5) return false;
    if (!comp_buf.empty()) {
        if (std::fwrite(comp_buf.data(), 1, comp_buf.size(), rf->fp)
              != comp_buf.size()) return false;
    }
    // Zero-pad to sector_count * 4096.
    size_t written = 5 + comp_buf.size();
    size_t total   = (size_t)sector_count * kSectorSize;
    if (written < total) {
        std::vector<uint8_t> pad(total - written, 0);
        if (std::fwrite(pad.data(), 1, pad.size(), rf->fp) != pad.size())
            return false;
    }
    std::fflush(rf->fp);

    // Update table entry.
    int local_x = cx & 31;
    int local_z = cz & 31;
    rf->table[RegionFile::tableIndex(local_x, local_z)] =
        (off & 0x00FFFFFF) | ((uint32_t)sector_count << 24);
    // Write the new table back to disk.
    return writeRegionTable(rf->fp, rf->region_x, rf->region_z, rf->table);
}

void McaCpp::evictOldestRegion() {
    // Phase 8 strategy: just drop the first element by iteration
    // order (unordered_map iteration is LRU-adjacent enough for
    // small maps). A true LRU needs a doubly-linked list, deferred
    // to v1.1.
    if (regions_.empty()) return;
    auto it = regions_.begin();
    if (it->second && it->second->fp) {
        std::fflush(it->second->fp);
        std::fclose(it->second->fp);
        it->second->fp = nullptr;
    }
    regions_.erase(it);
}

// ----- LevelProvider interface (chunk codec deferred to Phase 9).

std::optional<std::shared_ptr<FullChunk>>
McaCpp::loadChunk(int32_t x, int32_t z) {
    auto payload = readChunkPayload(x, z);
    if (!payload) return std::nullopt;
    // TODO Phase 9: ChunkNBTCodec::decode(payload) → mcp_chunk_t*
    // then wrap in ChunkAdapter. Until then, nullopt — the chunk
    // is on disk but we can't decode it yet.
    (void)payload;
    return std::nullopt;
}

void McaCpp::saveChunk(const std::shared_ptr<FullChunk>& chunk) {
    if (!chunk) return;

    // Get the raw mcp_chunk_t* via the ChunkAdapter.
    auto* adapter = dynamic_cast<ChunkAdapter*>(chunk.get());
    if (!adapter) return;
    mcp_chunk_t* raw = adapter->chunk();
    if (!raw) return;

    // Encode to NBT bytes (little-endian PMMP mcacpp format).
    size_t nbt_len = 0;
    uint8_t* nbt_buf = mcp_chunk_nbt_encode(raw, &nbt_len);
    if (!nbt_buf || nbt_len == 0) return;

    // Write the payload (zlib compression = 1, as PMMP mcacpp does).
    std::vector<uint8_t> payload(nbt_buf, nbt_buf + nbt_len);
    free(nbt_buf);

    bool ok = writeChunkPayload(raw->x, raw->z, /*compression=*/1, payload);
    if (!ok) {
        // writeChunkPayload already logs failure; nothing more to do.
        return;
    }
    // Clear dirty flag — chunk has been persisted.
    raw->dirty = false;
    raw->last_update = (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void McaCpp::unloadChunk(int32_t /*x*/, int32_t /*z*/) {
    // Phase 8: in-memory chunk cache is owned by Level (Phase 10),
    // not by the provider. No-op here.
}

std::vector<std::pair<int32_t, int32_t>>
McaCpp::listChunks() const {
    // TODO Phase 9: enumerate every region file, parse its table,
    // collect every non-zero entry's chunk coord. For Phase 8 we
    // return an empty list so callers know we have nothing on disk.
    return {};
}

// ----- Per-world metadata (level.dat).

void McaCpp::loadMetadata() {
    // Phase 8 stub: defaults are fine; full NBT reader path arrives
    // with Phase 9 ChunkNBTCodec. We just stat() the file so
    // `init()` doesn't pretend to know a seed that isn't there.
    std::string p = path_ + "/level.dat";
    FILE* fp = std::fopen(p.c_str(), "rb");
    if (!fp) {
        seed_  = 0;
        time_  = 0;
        spawn_ = math::Vector3{0.0, 64.0, 0.0};
        return;
    }
    std::fclose(fp);
    // TODO Phase 9: parse level.dat as NBT.
    seed_  = 0;
    time_  = 0;
    spawn_ = math::Vector3{0.0, 64.0, 0.0};
}

void McaCpp::saveMetadata() {
    // TODO Phase 9: serialize seed/spawn/time to level.dat.
    std::string p = path_ + "/level.dat";
    FILE* fp = std::fopen(p.c_str(), "wb");
    if (!fp) return;
    // Minimal stub: write a placeholder so the next init() can find
    // the file. Real NBT lands in Phase 9.
    uint8_t z[1] = {0};
    std::fwrite(z, 1, 1, fp);
    std::fclose(fp);
}

} // namespace cenisyspe::level::format::mcacpp

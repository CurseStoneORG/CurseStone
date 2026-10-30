/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// CurseStone | vgen — vanilla 1.0.0 overworld generator support layer.

#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <array>
#include <algorithm>
#include <limits>
#include <type_traits>

namespace vgen {

using std::make_unique;
using std::make_shared;
using LockGuard = std::lock_guard<std::mutex>;

// ---------------------------------------------------------------------------
// Debug / profile stubs — the restored sources sprinkle these everywhere.
// ---------------------------------------------------------------------------
#define DEBUG_ASSERT(cond, msg) ((void)0)
#define UNUSED_VARIABLE(x) (void)(x)
#define UNUSED_PARAMETER(x) (void)(x)
#define DEBUG_FAIL(msg) ((void)0)
#define LOGW(...) ((void)0)
#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)
#define LOGD(...) ((void)0)
struct ScopedProfile {
        explicit ScopedProfile(const char*) {}
};

// ---------------------------------------------------------------------------
// Common types (CommonTypes.h subset)
// ---------------------------------------------------------------------------
using RandomSeed = uint32_t;
typedef int16_t Height;
typedef uint8_t DataID;
typedef int_fast8_t FacingID;

enum class BlockProperty : uint32_t {
        Unspecified = 0,
        Stair = (1 << 0),
        HalfSlab = (1 << 1),
        Hopper = (1 << 2),
        TopSnow = (1 << 3),
        FenceGate = (1 << 4),
        Leaf = (1 << 5),
        ThinConnects2D = (1 << 6),
        Connects2D = (1 << 7),
        Carpet = (1 << 8),
        Door = (1 << 10)
};

struct BlockID {
        uint8_t value = 0;
        BlockID() = default;
        BlockID(uint8_t v) : value(v) {}
        operator uint8_t() const { return value; }
        BlockID& operator=(uint8_t v) { value = v; return *this; }
        bool operator==(const BlockID& o) const { return value == o.value; }
        bool operator!=(const BlockID& o) const { return value != o.value; }
        // int overloads only: an exact-match right operand keeps expressions
        // like `mID == 53` unambiguous against the built-in operator==(int,
        // int). With an extra uint8_t overload clang (C2666-style ambiguity)
        // and MSVC both reject the mix of implicit BlockID/uint8_t/int
        // conversions; every integral operand reaches the int overload via
        // promotion or a single standard conversion, which always beats the
        // user-defined conversion route of the built-in candidate.
        bool operator==(int v) const { return value == (uint8_t)v; }
        bool operator!=(int v) const { return value != (uint8_t)v; }
        static const BlockID AIR;
        bool hasProperty(BlockProperty prop) const;
};

// Generator type (world/level/GeneratorType.h subset)
enum class GeneratorType : int {
        Legacy,
        Overworld,
        Flat,
        Nether,
        TheEnd,
        Undefined
};

class Block;
struct FullBlock {
        static const FullBlock AIR;
        const Block& getBlock() const;

        BlockID id;
        DataID data;

        static FullBlock fromInt(int i) {
                return FullBlock((BlockID)(uint8_t)(i & 0xff), (DataID)(uint8_t)(i >> 8));
        }
        FullBlock() : id(0), data(0) {}
        FullBlock(BlockID i) : id(i), data(0) {}
        FullBlock(BlockID i, DataID d) : id(i), data(d) {}

        bool operator!=(const FullBlock& other) const {
                return id.value != other.id.value || data != other.data;
        }
        bool operator==(const FullBlock& other) const {
                return id.value == other.id.value && data == other.data;
        }
        int toInt() const { return id.value + (data << 8); }
        bool isAir() const { return id.value == 0; }
};

class Feature;
typedef std::unique_ptr<Feature> FeaturePtr;

template <class T>
using Unique = std::unique_ptr<T>;

// enum_cast: the restored sources use it to fold an enum value down to
// its underlying integer type (the generator passes it as block data).
template <typename E>
inline typename std::underlying_type<E>::type enum_cast(E v) {
        return static_cast<typename std::underlying_type<E>::type>(v);
}

// ---------------------------------------------------------------------------
// Small math helper (Math.h subset used by the generator)
// ---------------------------------------------------------------------------
struct Math {
        static const float PI;
        static float sqrt(float x) { return ::sqrtf(x); }
        static float sin(float v) { return ::sinf(v); }
        static float cos(float v) { return ::cosf(v); }
        static int floor(float v) {
                return (int)::floorf(v);
        }
        static int ceil(float v) {
                int i = (int)v;
                return v > 0.0f && (float)i != v ? i + 1 : i;
        }
        static int abs(int v) { return v < 0 ? -v : v; }
        static float abs(float v) { return v < 0.0f ? -v : v; }
        static float clamp(float v, float low, float high) {
                return v < low ? low : (v > high ? high : v);
        }
        static float clampedLerp(float lo, float hi, float factor) {
                if (factor < 0.0f) return lo;
                if (factor > 1.0f) return hi;
                return lo + (hi - lo) * factor;
        }
        static float log2(float t) {
                static const float INV_LOG_2 = 1.0f / ::logf(2.0f);
                return ::logf(t) * INV_LOG_2;
        }
        static int nextInt(struct Random& random, int minInclusive, int maxInclusive);
        static float nextFloat(struct Random& random, float min, float max);
        static double nextDouble(struct Random& random, double min, double max);
        static float wrapDegrees(float input) {
                input = ::fmodf(input + 180.0f, 360.0f);
                if (input < 0.0f) input += 360.0f;
                return input - 180.0f;
        }
        // Return by value on purpose: for same-type arguments the
        // conditional yields an lvalue, so a plain decltype would make
        // these return a reference to a parameter that dies on return
        // and optimizers turn the result into garbage.
        template <typename T1, typename T2>
        static auto max(T1 a, T2 b) ->
                typename std::decay<decltype(a > b ? a : b)>::type {
                return a > b ? a : b;
        }
        template <typename T1, typename T2>
        static auto min(T1 a, T2 b) ->
                typename std::decay<decltype(a < b ? a : b)>::type {
                return a < b ? a : b;
        }
};

// ---------------------------------------------------------------------------
// Ranges (range.h verbatim semantics)
// ---------------------------------------------------------------------------
template <typename INDEX, INDEX STEP = 1>
class Range {
        const INDEX mBeginIDX, mEndIDX;

public:
        class iterator {
                INDEX mIndex;

        public:
                iterator(INDEX i) : mIndex(i) {}
                void operator++() { mIndex += STEP; }
                INDEX operator*() const { return mIndex; }
                bool operator!=(const iterator& other) const { return mIndex != other.mIndex; }
        };

        Range(INDEX beginIDX, INDEX endIDX) : mBeginIDX(beginIDX), mEndIDX(endIDX) {}

        const iterator begin() const { return iterator(mBeginIDX); }
        const iterator end() const { return iterator(mEndIDX); }
};

template <typename INDEX, INDEX STEP = 1>
Range<INDEX, STEP> range(INDEX begin, INDEX end) {
        return Range<INDEX, STEP>(begin, end);
}
template <typename INDEX, INDEX STEP = 1>
Range<INDEX, STEP> range(INDEX count) {
        return Range<INDEX, STEP>(0, count);
}
template <typename INDEX, INDEX STEP = 1>
Range<INDEX, STEP> range_incl(INDEX begin, INDEX end) {
        return Range<INDEX, STEP>(begin, end + STEP);
}
template <typename INDEX, INDEX STEP = 1>
Range<INDEX, STEP> range_incl(INDEX count) {
        return Range<INDEX, STEP>(0, count + STEP);
}
template <typename INDEX, int STEP = -1>
Range<int, STEP> reverse_range(INDEX begin, INDEX end) {
        return range<int, STEP>(((int)end) + STEP, ((int)begin) + STEP);
}
template <typename INDEX, int STEP = -1>
Range<int, STEP> reverse_range(INDEX count) {
        return reverse_range<INDEX, STEP>(0, count);
}

// ---------------------------------------------------------------------------
// buffer_span — minimal stand-in for Core/Utility/buffer_span.h
// ---------------------------------------------------------------------------
template <typename T>
class buffer_span_mut;

template <typename T>
class buffer_span {
        const T* mBegin = nullptr;
        const T* mEnd = nullptr;

public:
        buffer_span() = default;
        buffer_span(const T* b, const T* e) : mBegin(b), mEnd(e) {}
        buffer_span(const buffer_span_mut<T>& mut)
            : mBegin(mut.begin()), mEnd(mut.end()) {}
        const T* begin() const { return mBegin; }
        const T* end() const { return mEnd; }
        size_t byte_size() const { return (size_t)(mEnd - mBegin) * sizeof(T); }
        size_t size() const { return (size_t)(mEnd - mBegin); }
        const T& operator[](size_t i) const { return mBegin[i]; }
};

template <typename T>
class buffer_span_mut {
        T* mBegin = nullptr;
        T* mEnd = nullptr;

public:
        buffer_span_mut() = default;
        buffer_span_mut(T* b, T* e) : mBegin(b), mEnd(e) {}
        T* begin() const { return mBegin; }
        T* end() const { return mEnd; }
        size_t byte_size() const { return (size_t)(mEnd - mBegin) * sizeof(T); }
        size_t size() const { return (size_t)(mEnd - mBegin); }
        T& operator[](size_t i) const { return mBegin[i]; }
        buffer_span_mut<T> tail(size_t idx) const { return buffer_span_mut<T>(mBegin + idx, mEnd); }
};

struct NibblePair {
        uint8_t first : 4;
        uint8_t second : 4;
};

// Brightness (CommonTypes.h subset) — light levels are 0..15 nibbles.
struct Brightness {
        uint8_t value = 0;
        Brightness() = default;
        Brightness(uint8_t v) : value(v) {}
        operator uint8_t() const { return value; }
        static const Brightness MIN;
        static const Brightness MAX;
        bool operator<=(const Brightness& o) const { return value <= o.value; }
        bool operator<(const Brightness& o) const { return value < o.value; }
        bool operator<(int v) const { return value < v; }
        bool operator<=(int v) const { return value <= v; }
        bool operator>(int v) const { return value > v; }
        bool operator>=(int v) const { return value >= v; }
        bool operator==(int v) const { return value == v; }
        bool operator!=(int v) const { return value != v; }
        Brightness operator-(const Brightness& o) const {
                return Brightness((uint8_t)(value > o.value ? value - o.value : 0));
        }
};

// ---------------------------------------------------------------------------
// Colors (Core/Math/Color.h subset — generator only compares/int formats)
// ---------------------------------------------------------------------------
struct Color {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        Color() = default;
        Color(uint8_t rr, uint8_t gg, uint8_t bb, uint8_t aa = 255) : r(rr), g(gg), b(bb), a(aa) {}
        static const Color NIL;
        static Color fromHSB(float hue, float saturation, float brightness) {
                float h = hue - ::floorf(hue);
                h = h * 6.0f;
                float f = h - ::floorf(h);
                float p = brightness * (1.0f - saturation);
                float q = brightness * (1.0f - f * saturation);
                float t = brightness * (1.0f - (1.0f - f) * saturation);
                float r, g, b;
                switch ((int)h % 6) {
                        case 0: r = brightness; g = t; b = p; break;
                        case 1: r = q; g = brightness; b = p; break;
                        case 2: r = p; g = brightness; b = t; break;
                        case 3: r = p; g = q; b = brightness; break;
                        case 4: r = t; g = p; b = brightness; break;
                        default: r = brightness; g = p; b = q; break;
                }
                return Color((uint8_t)(r * 255.0f), (uint8_t)(g * 255.0f), (uint8_t)(b * 255.0f));
        }
        static Color fromARGB(uint32_t argb) {
                return Color((uint8_t)((argb >> 16) & 0xff), (uint8_t)((argb >> 8) & 0xff),
                             (uint8_t)(argb & 0xff), (uint8_t)((argb >> 24) & 0xff));
        }
        bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
        bool operator!=(const Color& o) const { return !(*this == o); }
        uint32_t toARGB() const { return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
};

// ---------------------------------------------------------------------------
// Vectors / positions
// ---------------------------------------------------------------------------
struct Vec2 {
        float x = 0, y = 0;
        Vec2() = default;
        Vec2(float xx, float yy) : x(xx), y(yy) {}
        Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
        Vec2 operator/(float s) const { return Vec2(x / s, y / s); }
        Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
        Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
};

struct BlockPos;
struct Vec3 {
        float x = 0, y = 0, z = 0;
        Vec3() = default;
        Vec3(float xx, float yy, float zz) : x(xx), y(yy), z(zz) {}
        Vec3(const BlockPos& p);

        float lengthSquared() const { return x * x + y * y + z * z; }
        float length() const { return ::sqrtf(lengthSquared()); }
        Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
        Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
        Vec3 operator+(float s) const { return Vec3(x + s, y + s, z + s); }
        Vec3 operator-(float s) const { return Vec3(x - s, y - s, z - s); }
        Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
        Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
        Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
        Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
        Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
        Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
};

struct Pos {
        int x = 0, y = 0, z = 0;

        Pos() = default;
        Pos(int xx, int yy, int zz) : x(xx), y(yy), z(zz) {}
        Pos(const Pos& p) : x(p.x), y(p.y), z(p.z) {}

        bool operator==(const Pos& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
        bool operator!=(const Pos& rhs) const { return !(*this == rhs); }
        Pos operator-(const Pos& rhs) const { return Pos(x - rhs.x, y - rhs.y, z - rhs.z); }
        Pos operator+(const Pos& rhs) const { return Pos(x + rhs.x, y + rhs.y, z + rhs.z); }
        Pos operator+(int o) const { return Pos(x + o, y + o, z + o); }
        Pos operator*(int s) const { return Pos(x * s, y * s, z * s); }
        Pos offset(int dx, int dy, int dz) const { return Pos(x + dx, y + dy, z + dz); }
        void set(int xx, int yy, int zz) { x = xx; y = yy; z = zz; }
        void set(const Pos& p) { x = p.x; y = p.y; z = p.z; }
        int hashCode() const { return x + (z << 8) + (y << 16); }
};

class ChunkPos;
struct BlockPos : public Pos {
        BlockPos(const ChunkPos& cp, int y = 0);
        static const BlockPos ZERO;
        static const BlockPos ONE;
        static const BlockPos MIN;
        static const BlockPos MAX;

        BlockPos() = default;
        explicit BlockPos(int a) : Pos(a, a, a) {}
        BlockPos(int xx, int yy, int zz) : Pos(xx, yy, zz) {}
        BlockPos(const Pos& p) : Pos(p) {}
        BlockPos(const Vec3& v) : Pos((int)::floorf(v.x), (int)::floorf(v.y), (int)::floorf(v.z)) {}
        BlockPos(float xx, float yy, float zz)
            : Pos((int)::floorf(xx), (int)::floorf(yy), (int)::floorf(zz)) {}

        BlockPos operator+(const BlockPos& o) const { return BlockPos(x + o.x, y + o.y, z + o.z); }
        BlockPos operator+(const Pos& o) const { return BlockPos(x + o.x, y + o.y, z + o.z); }
        BlockPos operator-(const BlockPos& o) const { return BlockPos(x - o.x, y - o.y, z - o.z); }
        BlockPos operator*(int s) const { return BlockPos(x * s, y * s, z * s); }
        BlockPos below() const { return BlockPos(x, y - 1, z); }
        BlockPos above() const { return BlockPos(x, y + 1, z); }
        BlockPos above(int steps) const { return BlockPos(x, y + steps, z); }
        BlockPos below(int steps) const { return BlockPos(x, y - steps, z); }
        BlockPos west() const { return BlockPos(x - 1, y, z); }
        BlockPos east() const { return BlockPos(x + 1, y, z); }
        BlockPos north() const { return BlockPos(x, y, z - 1); }
        BlockPos south() const { return BlockPos(x, y, z + 1); }
        BlockPos west(int s) const { return BlockPos(x - s, y, z); }
        BlockPos east(int s) const { return BlockPos(x + s, y, z); }
        BlockPos north(int s) const { return BlockPos(x, y, z - s); }
        BlockPos south(int s) const { return BlockPos(x, y, z + s); }
        BlockPos relative(FacingID face) const;
        int distSqr(int x2, int y2, int z2) const {
                int dx = x2 - x, dy = y2 - y, dz = z2 - z;
                return dx * dx + dy * dy + dz * dz;
        }
        int distSqr(const BlockPos& o) const { return distSqr(o.x, o.y, o.z); }
        BlockPos& operator+=(const BlockPos& o) { x += o.x; y += o.y; z += o.z; return *this; }
        BlockPos& operator-=(const BlockPos& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
        BlockPos offset(int dx, int dy, int dz) const { return BlockPos(x + dx, y + dy, z + dz); }
        static BlockPos max(const BlockPos& a, const BlockPos& b) {
                return BlockPos(::vgen::Math::max(a.x, b.x), ::vgen::Math::max(a.y, b.y), ::vgen::Math::max(a.z, b.z));
        }
        static BlockPos min(const BlockPos& a, const BlockPos& b) {
                return BlockPos(::vgen::Math::min(a.x, b.x), ::vgen::Math::min(a.y, b.y), ::vgen::Math::min(a.z, b.z));
        }
};

struct ChunkPos {
        int x = 0, z = 0;

        ChunkPos() = default;
        ChunkPos(int xx, int zz) : x(xx), z(zz) {}
        explicit ChunkPos(const BlockPos& pos) : x(pos.x >> 4), z(pos.z >> 4) {}
        explicit ChunkPos(const Vec3& pos)
            : ChunkPos(BlockPos(pos.x, pos.y, pos.z)) {}

        bool operator==(const ChunkPos& o) const { return x == o.x && z == o.z; }
        bool operator!=(const ChunkPos& o) const { return !(*this == o); }
        ChunkPos operator+(const ChunkPos& o) const { return ChunkPos(x + o.x, z + o.z); }
        ChunkPos operator-(const ChunkPos& o) const { return ChunkPos(x - o.x, z - o.z); }
        ChunkPos operator+(int t) const { return ChunkPos(x + t, z + t); }
        ChunkPos operator-(int t) const { return ChunkPos(x - t, z - t); }
        int hashCode() const { return x + (z << 8); }
        static int hashCode(int xx, int zz) { return xx + (zz << 8); }
        int distanceToSqr(const ChunkPos& p) const {
                int a = p.x - x, b = p.z - z;
                return a * a + b * b;
        }
};

struct ChunkBlockPos {
        uint8_t x = 0, z = 0;
        Height y = 0;

        ChunkBlockPos() = default;
        explicit ChunkBlockPos(uint8_t i) : x(i), y(i), z(i) {}
        ChunkBlockPos(uint8_t xx, Height yy, uint8_t zz) : x(xx), y(yy), z(zz) {}
        explicit ChunkBlockPos(const BlockPos& pos)
            : x((uint8_t)(pos.x & 15)), y((Height)pos.y), z((uint8_t)(pos.z & 15)) {}

        uint16_t index() const {
                return (uint16_t)(y + (z + x * 16) * 128);
        }
        uint8_t index2D() const { return (uint8_t)(z * 16 + x); }
        bool isInChunk() const { return y >= 0 && y < 128; }
        bool isInChunk2D() const { return x < 16 && z < 16; }

        BlockPos operator+(const BlockPos& o) const {
                return BlockPos(x + o.x, y + o.y, z + o.z);
        }
        BlockPos operator+(const Pos& o) const {
                return BlockPos(x + o.x, y + o.y, z + o.z);
        }
        BlockPos above() const { return BlockPos(x, y + 1, z); }
        BlockPos below() const { return BlockPos(x, y - 1, z); }
};

// ---------------------------------------------------------------------------
// Level constants (LevelConstants.h subset)
// ---------------------------------------------------------------------------
const int LEVEL_SPAWN_HEIGHT = 32767; // INT16_MAX — used as an invalid spawn marker

const int CHUNK_WIDTH = 16;
const int CHUNK_DEPTH = CHUNK_WIDTH;
const int CHUNK_COLUMNS = CHUNK_WIDTH * CHUNK_DEPTH;
const Height LEVEL_GEN_HEIGHT = 128;

// ---------------------------------------------------------------------------
// BlockVolume — faithful port of world/level/block/BlockVolume.h.
// Layout is [x][z][y]: index = y + (z + x * width) * height.
// ---------------------------------------------------------------------------
class BlockVolume {
public:
        BlockVolume(buffer_span_mut<uint8_t> buffer, int width, int height, int depth)
            : mWidth(width), mHeight(height), mDepth(depth) {
                auto ptr = buffer.begin();
                blocks = buffer_span_mut<BlockID>(reinterpret_cast<BlockID*>(ptr),
                                                  reinterpret_cast<BlockID*>(ptr + getVolume()));
                ptr += blocks.byte_size();
                data = buffer_span_mut<NibblePair>(reinterpret_cast<NibblePair*>(ptr),
                                                   reinterpret_cast<NibblePair*>(ptr + getVolume() / 2));
        }

        BlockVolume(buffer_span_mut<BlockID> buffer, int width, int height, int depth)
            : mWidth(width), mHeight(height), mDepth(depth) {
                blocks = buffer;
        }

        buffer_span_mut<BlockID> blocks;
        buffer_span_mut<NibblePair> data;

        uint32_t getVolume() const { return (uint32_t)(mWidth * mHeight * mDepth); }
        uint32_t getHeight() const { return (uint32_t)mHeight; }

        uint32_t index(const Pos& pos) const {
                return (uint32_t)(pos.y + (pos.z + pos.x * mWidth) * mHeight);
        }
        uint32_t index(const ChunkBlockPos& pos) const {
                return (uint32_t)(pos.y + (pos.z + pos.x * mWidth) * mHeight);
        }

        BlockID& block(uint32_t i) { return blocks[i]; }
        BlockID& block(const Pos& pos) { return blocks[index(pos)]; }
        BlockID& block(const ChunkBlockPos& pos) { return blocks[index(pos)]; }

        DataID getData(const Pos& pos) const {
                auto idx = index(pos);
                auto& pair = data[idx / 2];
                return idx % 2 == 0 ? (DataID)pair.first : (DataID)pair.second;
        }
        void setBlock(uint32_t i, BlockID id) { blocks[i] = id; }
        void setBlock(const Pos& pos, BlockID id) { blocks[index(pos)] = id; }
        void setBlock(const ChunkBlockPos& pos, BlockID id) { blocks[index(pos)] = id; }
        void setData(const Pos& pos, DataID val) {
                auto idx = index(pos);
                auto& pair = data[idx / 2];
                if (idx % 2 == 0)
                        pair.first = (uint8_t)val;
                else
                        pair.second = (uint8_t)val;
        }
        void setData(const ChunkBlockPos& pos, DataID val) {
                setData(Pos{pos.x, pos.y, pos.z}, val);
        }

private:
        uint32_t mWidth, mHeight, mDepth;
};

} // namespace vgen

namespace std {
template <>
struct hash<::vgen::ChunkPos> {
        size_t operator()(const ::vgen::ChunkPos& p) const { return (size_t)p.hashCode(); }
};
template <>
struct hash<::vgen::Pos> {
        size_t operator()(const ::vgen::Pos& p) const { return (size_t)p.hashCode(); }
};
template <>
struct hash<::vgen::FullBlock> {
        size_t operator()(const ::vgen::FullBlock& b) const { return (size_t)b.toInt(); }
};
} // namespace std

// Vector3.hpp — C++ wrapper around mcp_vector3_t with operator overloads.
// Use this in C++ files; C files use Vector3.h directly.
// Protocol 84 (PocketMine 0.15.10): positions are doubles (float on wire).
#pragma once
#include "Vector3.h"
#include <cmath>
#include <string>

namespace cenisyspe::math {

struct Vec3 {
    double x{0}, y{0}, z{0};

    Vec3() = default;
    Vec3(double x, double y, double z) : x(x), y(y), z(z) {}
    explicit Vec3(mcp_vector3_t v) : x(v.x), y(v.y), z(v.z) {}

    mcp_vector3_t toC() const { return mcp_v3(x, y, z); }

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s)      const { return {x * s,   y * s,   z * s};   }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    bool operator==(const Vec3& o) const = default;

    double lengthSq() const { return x*x + y*y + z*z; }
    double length()   const { return std::sqrt(lengthSq()); }
    double distance(const Vec3& o) const { return (*this - o).length(); }
    Vec3   normalize() const {
        double l = length();
        return l > 0 ? *this * (1.0 / l) : *this;
    }
    Vec3 floor() const {
        return { std::floor(x), std::floor(y), std::floor(z) };
    }

    // Block-coordinate helpers (protocol 84 uses int block coords in packets).
    int32_t blockX() const { return static_cast<int32_t>(std::floor(x)); }
    int32_t blockY() const { return static_cast<int32_t>(std::floor(y)); }
    int32_t blockZ() const { return static_cast<int32_t>(std::floor(z)); }

    std::string toString() const {
        return "(" + std::to_string(x) + ", " +
                     std::to_string(y) + ", " +
                     std::to_string(z) + ")";
    }
};

} // namespace cenisyspe::math

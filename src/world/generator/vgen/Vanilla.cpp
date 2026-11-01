// CurseStone | vgen — out-of-line constant definitions for Vanilla.h.
#include "vgen/Vanilla.h"
#include "vgen/Facing.h"
#include "vgen/Random.h"

namespace vgen {

const float Math::PI = 3.14159265358979f;

int Math::nextInt(struct Random& random, int minInclusive, int maxInclusive) {
        if (minInclusive >= maxInclusive) {
                return minInclusive;
        }
        return random.nextInt(maxInclusive - minInclusive + 1) + minInclusive;
}

float Math::nextFloat(struct Random& random, float min, float max) {
        return min + random.nextFloat() * (max - min);
}

double Math::nextDouble(struct Random& random, double min, double max) {
        return min + random.nextDouble() * (max - min);
}

const BlockID BlockID::AIR{};

const BlockPos BlockPos::ZERO = BlockPos(0, 0, 0);
const BlockPos BlockPos::ONE = BlockPos(1, 1, 1);
const BlockPos BlockPos::MIN = BlockPos(-2147483647 - 1, 0, -2147483647 - 1);
const BlockPos BlockPos::MAX = BlockPos(2147483647, 127, 2147483647);

const FullBlock FullBlock::AIR = FullBlock(BlockID(0), DataID(0));

const Color Color::NIL = Color(0, 0, 0, 0);

const Brightness Brightness::MIN = Brightness(0);
const Brightness Brightness::MAX = Brightness(15);


BlockPos::BlockPos(const ChunkPos& cp, int y)
    : Pos(cp.x * 16, y, cp.z * 16) {}

BlockPos BlockPos::relative(FacingID face) const {
        const BlockPos* dir = &Facing::DIRECTION[face];
        return BlockPos(x + dir->x, y + dir->y, z + dir->z);
}

Vec3::Vec3(const struct BlockPos& p) : x((float)p.x), y((float)p.y), z((float)p.z) {}

} // namespace vgen

// tools/pickup_sim.cpp
//
// Measures the dropped-item behaviour that Player::onUpdate() +
// Level::tickDroppedItems() implement, using the SAME numbers the server
// uses, so the pickup volume can be checked instead of guessed:
//
//   * where a dropped item actually comes to rest (integrator port of
//     pocketmine\entity\Item::onUpdate + Entity::move), and
//   * whether the player standing in a given spot is inside the pickup
//     volume — level::itemWithinPickupVolume() from src/level/Level.h.
//
//   g++ -std=c++20 -Wall -Wextra -O0 -o /tmp/pickup_sim tools/pickup_sim.cpp
//   /tmp/pickup_sim
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <string>

// ---- world ----------------------------------------------------------
// Everything with y < GROUND_TOP is solid. A scenario may punch a 1-deep
// pit (the block the player just broke) somewhere in the XZ plane.
static const double GROUND_TOP = 66.0;
static int g_pitX0 = 0, g_pitX1 = 0, g_pitZ0 = 0, g_pitZ1 = 0;   // empty by default

static bool solid(int x, int y, int z) {
    if (y == (int)GROUND_TOP - 1 && x >= g_pitX0 && x < g_pitX1 &&
        z >= g_pitZ0 && z < g_pitZ1) return false;      // the pit floor is gone
    return (double)y < GROUND_TOP;
}

// ---- constants (src/level/Level.cpp + src/level/Level.h) -----------
static const double kItemGravity = 0.04, kItemDrag = 0.02;
static const double kItemHalfW = 0.125, kItemHeight = 0.25;
static const double kBlockFriction = 0.6;
static const double kPickupHalfExtent = 1.3;
static const double kPickupBelowFeet  = 1.0;
static const double kPickupAboveFeet  = 2.8;
static const double kAttractRadius = 2.0, kAttractSpeed = 0.35;

struct Aabb { double minX, minY, minZ, maxX, maxY, maxZ; };

struct Item {
    double x, y, z, vx, vy, vz;
    bool on_ground = false;
    int delay = 10;
};

// level::itemWithinPickupVolume()
static bool inPickup(const Item& it, double px, double py, double pz) {
    if (!(it.x + kItemHalfW > px - kPickupHalfExtent &&
          it.x - kItemHalfW < px + kPickupHalfExtent)) return false;
    if (!(it.y + kItemHeight > py - kPickupBelowFeet &&
          it.y             < py + kPickupAboveFeet)) return false;
    if (!(it.z + kItemHalfW > pz - kPickupHalfExtent &&
          it.z - kItemHalfW < pz + kPickupHalfExtent)) return false;
    return true;
}

static void tick(Item& di, double px, double py, double pz, bool magnet) {
    if (di.delay > 0) --di.delay;
    di.vy -= kItemGravity;

    {   // Entity::checkObstruction
        int i = (int)std::floor(di.x), j = (int)std::floor(di.y), k = (int)std::floor(di.z);
        double diffX = di.x - i, diffY = di.y - j, diffZ = di.z - k;
        if (solid(i, j, k)) {
            bool f0 = !solid(i-1,j,k), f1 = !solid(i+1,j,k), f2 = !solid(i,j-1,k);
            bool f3 = !solid(i,j+1,k), f4 = !solid(i,j,k-1), f5 = !solid(i,j,k+1);
            int dir = -1; double limit = 9999;
            if (f0) { limit = diffX; dir = 0; }
            if (f1 && 1-diffX < limit) { limit = 1-diffX; dir = 1; }
            if (f2 && diffY < limit) { limit = diffY; dir = 2; }
            if (f3 && 1-diffY < limit) { limit = 1-diffY; dir = 3; }
            if (f4 && diffZ < limit) { limit = diffZ; dir = 4; }
            if (f5 && 1-diffZ < limit) { dir = 5; }
            double force = (double)std::rand()/(double)RAND_MAX * 0.2 + 0.1;
            switch (dir) {
                case 0: di.vx = -force; break; case 1: di.vx = force; break;
                case 2: di.vy = -force; break; case 3: di.vy = force; break;
                case 4: di.vz = -force; break; case 5: di.vz = force; break;
                default: break;
            }
        }
    }

    if (di.on_ground && magnet) {
        if (!inPickup(di, px, py, pz)) {
            double adx = px-di.x, ady = (py+0.9)-di.y, adz = pz-di.z;
            double dist = std::sqrt(adx*adx+ady*ady+adz*adz);
            if (dist > 0.0001 && dist <= kAttractRadius) {
                double pull = 0.12*(1.0-dist/kAttractRadius)+0.03;
                di.vx += adx/dist*pull; di.vy += ady/dist*pull; di.vz += adz/dist*pull;
                double sp = std::sqrt(di.vx*di.vx+di.vy*di.vy+di.vz*di.vz);
                if (sp > kAttractSpeed) { double k = kAttractSpeed/sp; di.vx*=k; di.vy*=k; di.vz*=k; }
            }
        }
    }

    const double movX = di.vx, movY = di.vy, movZ = di.vz;
    {
        Aabb bb{ di.x-kItemHalfW, di.y, di.z-kItemHalfW,
                 di.x+kItemHalfW, di.y+kItemHeight, di.z+kItemHalfW };
        double dx = movX, dy = movY, dz = movZ;
        Aabb sw{ bb.minX+dx, bb.minY+dy, bb.minZ+dz, bb.maxX+dx, bb.maxY+dy, bb.maxZ+dz };
        int x0 = (int)std::floor(sw.minX), x1 = (int)std::ceil(sw.maxX);
        int y0 = (int)std::floor(sw.minY), y1 = (int)std::ceil(sw.maxY);
        int z0 = (int)std::floor(sw.minZ), z1 = (int)std::ceil(sw.maxZ);
        for (int bx=x0;bx<=x1;++bx) for (int by=y0;by<=y1;++by) for (int bz=z0;bz<=z1;++bz) {
            if (!solid(bx,by,bz)) continue;
            Aabb c{(double)bx,(double)by,(double)bz,(double)bx+1,(double)by+1,(double)bz+1};
            if (bb.maxX <= c.minX || bb.minX >= c.maxX) continue;
            if (bb.maxZ <= c.minZ || bb.minZ >= c.maxZ) continue;
            if (dy > 0 && bb.maxY <= c.minY) { double t = c.minY-bb.maxY; if (t < dy) dy = t; }
            if (dy < 0 && bb.minY >= c.maxY) { double t = c.maxY-bb.minY; if (t > dy) dy = t; }
        }
        di.y += dy; bb.minY += dy; bb.maxY += dy;
        for (int bx=x0;bx<=x1;++bx) for (int by=y0;by<=y1;++by) for (int bz=z0;bz<=z1;++bz) {
            if (!solid(bx,by,bz)) continue;
            Aabb c{(double)bx,(double)by,(double)bz,(double)bx+1,(double)by+1,(double)bz+1};
            if (bb.maxY <= c.minY || bb.minY >= c.maxY) continue;
            if (bb.maxZ <= c.minZ || bb.minZ >= c.maxZ) continue;
            if (dx > 0 && bb.maxX <= c.minX) { double t = c.minX-bb.maxX; if (t < dx) dx = t; }
            if (dx < 0 && bb.minX >= c.maxX) { double t = c.maxX-bb.minX; if (t > dx) dx = t; }
        }
        di.x += dx; bb.minX += dx; bb.maxX += dx;
        for (int bx=x0;bx<=x1;++bx) for (int by=y0;by<=y1;++by) for (int bz=z0;bz<=z1;++bz) {
            if (!solid(bx,by,bz)) continue;
            Aabb c{(double)bx,(double)by,(double)bz,(double)bx+1,(double)by+1,(double)bz+1};
            if (bb.maxX <= c.minX || bb.minX >= c.maxX) continue;
            if (bb.maxY <= c.minY || bb.minY >= c.maxY) continue;
            if (dz > 0 && bb.maxZ <= c.minZ) { double t = c.minZ-bb.maxZ; if (t < dz) dz = t; }
            if (dz < 0 && bb.minZ >= c.maxZ) { double t = c.maxZ-bb.minZ; if (t > dz) dz = t; }
        }
        di.z += dz;
        di.on_ground = (movY != dy && movY < 0);
        if (movX != dx) di.vx = 0;
        if (movY != dy) di.vy = 0;
        if (movZ != dz) di.vz = 0;
    }
    double friction = 1.0 - kItemDrag;
    if (di.on_ground && (std::fabs(di.vx) > 0.00001 || std::fabs(di.vz) > 0.00001))
        friction = kBlockFriction * friction;
    di.vx *= friction; di.vy *= (1.0-kItemDrag); di.vz *= friction;
    if (di.on_ground) di.vy *= -0.5;
}

static int failures = 0;

// The pit (the block the player broke) is given per scenario: it is the
// cell [px0,px1) x [pz0,pz1) at y = GROUND_TOP-1, whose floor is gone.
static void scenario(const char* name, Item it, double px, double py, double pz,
                     int ticks, bool magnet, bool expect_pickup,
                     int pitX0 = -1, int pitX1 = -1, int pitZ0 = -1, int pitZ1 = -1) {
    if (pitX0 >= 0) { g_pitX0 = pitX0; g_pitX1 = pitX1; g_pitZ0 = pitZ0; g_pitZ1 = pitZ1; }
    else            { g_pitX0 = g_pitX1 = g_pitZ0 = g_pitZ1 = 0; }
    int picked = -1;
    for (int t = 0; t < ticks; ++t) {
        tick(it, px, py, pz, magnet);
        if (it.delay == 0 && inPickup(it, px, py, pz)) { picked = t; break; }
    }
    bool got = picked >= 0;
    std::string res = got ? ("picked @tick " + std::to_string(picked)) : "NOT PICKED";
    printf("  [%s] %-46s rest=(%.3f, %.3f, %.3f)  %s\n",
           got == expect_pickup ? " OK " : "FAIL", name, it.x, it.y, it.z, res.c_str());
    if (got != expect_pickup) ++failures;
}

// Pure geometry check — no physics, for positions an item cannot rest at
// (a hovering item would simply fall into reach).
static void volume(const char* name, double ix, double iy, double iz,
                   double px, double py, double pz, bool expect) {
    Item it{ix, iy, iz, 0, 0, 0, false, 0};
    bool got = inPickup(it, px, py, pz);
    printf("  [%s] %-46s item=(%.2f, %.2f, %.2f) player=(%.2f, %.2f, %.2f) %s\n",
           got == expect ? " OK " : "FAIL", name, ix, iy, iz, px, py, pz,
           got ? "in reach" : "out of reach");
    if (got != expect) ++failures;
}

int main() {
    printf("flat ground: top = %.1f, player feet = %.1f\n", GROUND_TOP, GROUND_TOP);

    printf("\n-- THE REPORTED CASE: one block broken on the surface --\n");
    // The pit is the single block (10, 65, 9); its floor is gone, so the
    // drop rests at y = 65 — one block BELOW the feet of the player
    // standing on the surrounding surface (feet at 66). This is exactly
    // the drop that used to be uncollectable until the player dug a
    // second hole and stepped into it.
    scenario("player standing on the surface next to the pit",
             {10.5, 65.0, 9.5, 0.0, 0.0, 0.0}, 11.5, 66.0, 9.5, 200, false, true,
             10, 11, 9, 10);
    scenario("player digs a second hole and steps down",
             {10.5, 65.0, 9.5, 0.0, 0.0, 0.0}, 10.5, 65.0, 9.5, 200, false, true,
             10, 11, 9, 10);
    scenario("same, from the far side of the pit (2 blocks away)",
             {10.5, 65.0, 9.5, 0.0, 0.0, 0.0}, 12.5, 66.0, 9.5, 200, false, false,
             10, 11, 9, 10);

    printf("\n-- reach geometry (pure volume test) --\n");
    volume("item at the feet",                   11.0, 66.0,  9.5, 11.0, 66.0, 9.5, true);
    volume("item 1.0 below the feet (edge)",      11.0, 65.0,  9.5, 11.0, 66.0, 9.5, true);
    volume("item 1.5 below the feet",             11.0, 64.5,  9.5, 11.0, 66.0, 9.5, false);
    volume("item 1.4 away sideways",              11.0, 66.0,  9.5, 12.4, 66.0, 9.5, true);
    volume("item 1.6 away sideways",              11.0, 66.0,  9.5, 12.6, 66.0, 9.5, false);
    volume("item 2.5 above the feet (head+0.7)", 11.0, 68.5,  9.5, 11.0, 66.0, 9.5, true);
    volume("item 3.0 above the feet",             11.0, 69.0,  9.5, 11.0, 66.0, 9.5, false);
    volume("item 2.5 above + 1.6 sideways",       11.0, 68.5,  9.5, 12.6, 66.0, 9.5, false);
    volume("item at the feet, 1.4 diagonal",      11.0, 66.0,  9.5, 12.0, 66.0, 10.5, true);

printf("\n-- thrown item (Q) and pile behaviour --\n");
    scenario("Q drop, player 1 block away", {11.5, 67.3, 9.5, 0.15, 0.0, 0.0},
             11.5, 66.0, 9.5, 200, true, true);
    scenario("Q drop, player at the resting spot", {11.5, 67.3, 9.5, 0.15, 0.0, 0.0},
             12.7, 66.0, 9.5, 200, true, true);
    scenario("item 1.5 away, magnet pulls it in", {11.5, 67.3, 9.5, 0, 0, 0},
             13.0, 66.0, 9.5, 400, true, true);
    scenario("item 1.5 away, no magnet (out of reach)", {11.5, 67.3, 9.5, 0, 0, 0},
             13.0, 66.0, 9.5, 400, false, false);

    printf("\n%s (%d failure(s))\n", failures ? "PICKUP SIM: FAIL" : "PICKUP SIM: OK", failures);
    return failures ? 1 : 0;
}
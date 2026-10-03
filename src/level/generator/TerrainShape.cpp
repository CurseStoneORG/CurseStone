// SPDX-License-Identifier: LGPL-3.0-or-later
#include "level/generator/TerrainShape.h"
#include "level/generator/biome/BiomeRegistry.h"
#include "level/generator/biome/Biome.h"

#include <algorithm>

namespace cursestone::level::generator {

namespace {

inline double smoothstep(double e0, double e1, double x) {
    double t = (x - e0) / (e1 - e0);
    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;
    return t * t * (3.0 - 2.0 * t);
}

// Land mask: 0 over deep ocean, 1 on the continent.
inline double landMask(double cont) { return smoothstep(-0.28, 0.02, cont); }

} // namespace

double continentField(const TerrainNoise& n, double wx, double wz) {
    return mcp_simplex_fbm2d(n.height, wx * 0.0010, wz * 0.0010, 3, 0.5, 2.0);
}

double plainsFactor(const TerrainNoise& n, double wx, double wz) {
    const double cont = continentField(n, wx, wz);
    // Two terms. A biome-scale patch noise so plains also occur well inland,
    // and a coastal band keyed off `cont` so that the shelf of land just
    // above the ocean is almost always plains. The coastal term is what
    // puts the wide open grassland right against the sea, with hills and
    // forest further back — the shape the reference generator produces.
    const double p = mcp_simplex_fbm2d(n.surface, wx * 0.0017 + 3300.0,
                                       wz * 0.0017 - 1700.0, 2, 0.5, 2.0);
    const double inland = smoothstep(0.02, 0.40, p);
    const double coast = smoothstep(-0.12, 0.10, cont) *
                         (1.0 - smoothstep(0.14, 0.38, cont));
    return std::clamp(std::max(inland * 0.85, coast * 0.95), 0.0, 1.0);
}

double tempAt(const TerrainNoise& n, double wx, double wz) {
    return mcp_simplex_fbm2d(n.temp, wx * 0.0004, wz * 0.0004, 2, 0.5, 2.0);
}

double humidAt(const TerrainNoise& n, double wx, double wz) {
    return mcp_simplex_fbm2d(n.humid, wx * 0.0005 + 512.0,
                             wz * 0.0005 - 256.0, 2, 0.5, 2.0);
}

double plainsPlateau(const TerrainNoise& n, double wx, double wz) {
    // ~120-block patches that stand the grassland a few blocks higher than
    // the plain next to it and keep it flat on top: big enough to read as a
    // plateau you can walk across, small enough to be a VARIANT inside a
    // plain rather than another continent. Everything is gated on
    // plainsFactor, which is the same factor the relief damping and the
    // biome choice read, so the raised table, the flat ground and the
    // plains biome always agree on where a plain is.
    const double patch = smoothstep(0.02, 0.40,
        mcp_simplex_fbm2d(n.surface, wx * 0.0060 + 9100.0,
                          wz * 0.0060 - 2700.0, 2, 0.5, 2.0));
    if (patch <= 0.0) return 0.0;
    return patch * smoothstep(0.45, 0.72, plainsFactor(n, wx, wz));
}

double plainsForest(const TerrainNoise& n, double wx, double wz) {
    // Woodland-scale patches inside the grassland. Same idea as the
    // plateau: the biome selection turns this into BIOME_FOREST, so the
    // patch gets real forest density instead of a handful of stray oaks.
    const double patch = smoothstep(0.20, 0.48,
        mcp_simplex_fbm2d(n.surface, wx * 0.0062 - 6100.0,
                          wz * 0.0062 + 2450.0, 2, 0.5, 2.0));
    if (patch <= 0.0) return 0.0;
    return patch * smoothstep(0.45, 0.70, plainsFactor(n, wx, wz));
}

double savannaFactor(const TerrainNoise& n, double wx, double wz) {
    const double t  = tempAt(n, wx, wz);
    const double hu = humidAt(n, wx, wz);
    // Ask the biome selection directly whether this climate is a savanna.
    // Hand-rolling the temperature/humidity cuts again would be one more
    // place for the flattened ground and the biome byte to disagree — and
    // the height is passed at sea level + 4 so the ocean/mountain overrides
    // in select() can never answer here.
    if (biome::BiomeRegistry::select(t, hu, kSeaLevel + 4, kSeaLevel) !=
        biome::BIOME_SAVANNA)
        return 0.0;
    // Long stretches rather than a uniform sheet: the low-relief savanna
    // comes in patches a few hundred blocks wide. The mask is deliberately
    // low-frequency and the ramp is wide, because a narrow one left the
    // savanna (and the plateau cut out of it) as thin slivers between the
    // plains and the desert. The wavelength was lowered a touch (0.0032 ->
    // 0.0024) and the ramp widened so the savanna — and the table cut out
    // of it — read as broad country instead of ribbons.
    return smoothstep(-0.24, 0.20,
        mcp_simplex_fbm2d(n.surface, wx * 0.0024 + 4200.0,
                          wz * 0.0024 - 8800.0, 2, 0.5, 2.0));
}

// Desert proximity in [0,1]: how close this climate is to the hot/dry
// desert side of the savanna band. The savanna/desert boundary is the
// humid = -0.20 cut inside the temp > 0.5 band (see BiomeRegistry::select),
// so this ramps up smoothly as the humidity drops towards that cut and is
// exactly 1 for a genuine desert. The player wants savanna plateaus to
// always form next to the desert, which is what this drives below.
static double desertNearby(const TerrainNoise& n, double wx, double wz) {
    const double t  = tempAt(n, wx, wz);
    const double hu = humidAt(n, wx, wz);
    // A desert exists ONLY in the hot band (temp > 0.5). In the cooler
    // savanna band (0.30 < temp <= 0.5) there is no desert to be next to,
    // so this must stay 0 there or every warm savanna would be forced into
    // a plateau.
    if (t <= 0.50) return 0.0;
    // hu = -0.20 is the desert cut; ramp over ~0.30 of humidity so a plateau
    // starts to favour the desert side well before the biome actually flips.
    return smoothstep(-0.50, -0.20, hu);
}

double savannaCanyon(const TerrainNoise& n, double wx, double wz) {
    // Hard-gated to the CORE of a savanna, so a canyon can never appear in
    // (or next to) the plains, the desert or the jungle. The ramp starts a
    // little below the savanna threshold (0.45) so the table covers a wide,
    // contiguous share of the savanna instead of only its deepest core — a
    // higher threshold is what made the tables come out as thin ribbons.
    const double sf = savannaFactor(n, wx, wz);
    const double core = smoothstep(0.40, 0.55, sf);
    if (core <= 0.0) return 0.0;

    // Desert neighbourhood. When a savanna touches a desert the player wants
    // the raised table to ALWAYS show up: it is the classic warm-country
    // skyline. `desert` is high as the climate nears the desert cut.
    const double desert = desertNearby(n, wx, wz);

    // How far INSIDE the savanna we are. The old code vetoed the column
    // outright unless ALL eight neighbours 6 blocks out were still savanna,
    // which chopped the table into thin fingers along every boundary. Here
    // the four cardinal neighbours are combined with min() and turned into a
    // smooth interior weight, so the table keeps a clean edge but stays one
    // solid blob instead of a fringe.
    constexpr double kM = 6.0;
    double m = sf;
    m = std::min(m, savannaFactor(n, wx - kM, wz));
    m = std::min(m, savannaFactor(n, wx + kM, wz));
    m = std::min(m, savannaFactor(n, wx, wz - kM));
    m = std::min(m, savannaFactor(n, wx, wz + kM));
    const double interior = smoothstep(0.42, 0.60, m);
    if (interior <= 0.0) return 0.0;

    // ...and inland: a canyon never overlooks a shoreline, so no beach or
    // ocean can end up sitting next to one. `continentField` is the cheap
    // proxy for "is there land here" — it is the same field the coast is
    // built from, and it cannot recurse back into the terrain height the
    // way a rawColumnHeight() probe would.
    constexpr double kInland = -0.10;  // the shoreline/beach band sits below this
    if (continentField(n, wx - kM, wz) < kInland ||
        continentField(n, wx + kM, wz) < kInland ||
        continentField(n, wx, wz - kM) < kInland ||
        continentField(n, wx, wz + kM) < kInland)
        return 0.0;
    const double notPlains =
        1.0 - smoothstep(0.35, 0.55, plainsFactor(n, wx, wz));
    if (notPlains <= 0.0) return 0.0;
    // Broad, low-frequency table mask: the tables come out as big blobs
    // hundreds of blocks across rather than as small mounds or ribbons.
    double patch = smoothstep(-0.22, 0.14,
        mcp_simplex_fbm2d(n.surface, wx * 0.0016 + 15500.0,
                          wz * 0.0016 + 7300.0, 2, 0.5, 2.0));
    // Near the desert the table is wanted on essentially every savanna
    // column, so the patch mask is lifted towards 1 there.
    if (desert > 0.0) patch = std::max(patch, desert);
    return core * interior * notPlains * patch;
}

TerrainNoise::TerrainNoise(int64_t seed) {
    height  = mcp_simplex_new(seed);
    temp    = mcp_simplex_new(seed ^ 0xBA5EBA11);
    humid   = mcp_simplex_new(seed ^ 0x0FF1CE);
    river   = mcp_simplex_new(seed ^ 0x517E12);
    surface = mcp_simplex_new(seed ^ 0x9E3779B9);
}

TerrainNoise::~TerrainNoise() {
    mcp_simplex_destroy(height);
    mcp_simplex_destroy(temp);
    mcp_simplex_destroy(humid);
    mcp_simplex_destroy(river);
    mcp_simplex_destroy(surface);
}

double rawColumnHeight(const TerrainNoise& n, double wx, double wz) {
    // Continent shape (oceans vs. land). Long wavelength.
    const double cont = continentField(n, wx, wz);
    // Broad rolling swells — the large-scale swell that makes hills read as
    // hills instead of as one uniform bump field.
    const double broad = mcp_simplex_fbm2d(n.height, wx * 0.0022 + 313.0,
                                           wz * 0.0022 + 727.0, 3, 0.5, 2.0);
    // Rolled hills.
    const double hill = mcp_simplex_fbm2d(n.height, wx * 0.0055 + 77.3,
                                          wz * 0.0055 - 41.7, 4, 0.5, 2.0);
    // Small surface detail — this is what keeps the terrain from looking
    // like a smooth ramp.
    const double det = mcp_simplex_fbm2d(n.height, wx * 0.0140 + 900.0,
                                         wz * 0.0140 + 300.0, 3, 0.5, 2.0);
    // Fine crags. The long-wavelength layers barely move between two
    // neighbouring columns, so without a short-wavelength layer the ground
    // reads as glassy smooth no matter how big the hills are. This is what
    // actually gives the surface its local roughness.
    const double crag = mcp_simplex_fbm2d(n.height, wx * 0.0320 - 555.0,
                                          wz * 0.0320 + 111.0, 2, 0.5, 2.0);
    // Ridged mountains: a value that folds at 0 into sharp crests.
    const double ridgeN = mcp_simplex_fbm2d(n.height, wx * 0.0032 + 1234.5,
                                            wz * 0.0032 - 987.6, 3, 0.5, 2.0);
    double ridge = 1.0 - std::fabs(ridgeN);
    ridge *= ridge;
    // Sharp rocky outcrops. Every other layer here is low-frequency, so it
    // can only ever move the ground by a block or two between neighbouring
    // columns — which is why the hills read as smooth ramps. This is a
    // higher-frequency folded noise, masked to high ground only, and it is
    // what finally produces real 3-block steps and bare rock faces on the
    // hills without touching the lowland. Keeping it out of the lowlands is
    // also what stops it from making the river banks harsher.
    const double outN = mcp_simplex_fbm2d(n.height, wx * 0.0210 + 88.0,
                                          wz * 0.0210 - 55.0, 2, 0.5, 2.0);
    double outcrop = 1.0 - std::fabs(outN);
    outcrop = outcrop * outcrop * outcrop;

    const double land = landMask(cont);
    // Plains are genuinely flat: the relief layers are damped where
    // plainsFactor says we are on open grassland. This is what makes the
    // flat areas WIDE instead of a patch of flat ground surrounded by
    // hills, and the biome choice reads the same factor so "plains" means
    // the same flat thing in both places.
    //
    // The savanna plains use the very same damping (savannaFactor), so a
    // savanna comes out as long, level ground a couple of blocks high, and
    // the canyon table below rides on top of that.
    const double plains = plainsFactor(n, wx, wz);
    const double savanna = savannaFactor(n, wx, wz);
    // 0.90 rather than 0.78: at full plains the relief was still 22% of the
    // hills and the measured step between neighbouring plains columns came
    // out no flatter than the rest of the world, which is the opposite of
    // what makes a plain read as a plain.
    //
    // The savanna is damped a little less (0.78) so its surface keeps the
    // 2-3 block roll the plains deliberately shave off.
    const double damp = (savanna > plains) ? 0.78 : 0.90;
    const double relief = 1.0 - damp * std::max(plains, savanna);

    double h = 70.0 + cont * 31.0;                       // 39 .. 101 base
    // The broad swell is damped too. It was the one layer still at full
    // strength inside a plain, and since it is the largest term it is the
    // one that decides whether two neighbouring columns differ at all.
    h += broad * (13.0 * land * (0.30 + 0.70 * relief) + 1.5 * (1.0 - land));
    h += hill * (16.0 * land * relief + 3.0 * (1.0 - land));      // hills
    h += ridge * 24.0 * land * relief * smoothstep(0.30, 0.62, cont); // mountains
    // Detail is boosted where the ground is already high: highland gets
    // craggier, lowland stays smooth, which is what vanilla does.
    const double highland = smoothstep(70.0, 92.0, h);
    h += det * (3.6 * land * relief + 0.25) * (1.0 + 0.8 * highland);
    h += crag * (5.5 * land * relief + 0.12);    h += (outcrop - 0.30) * 9.5 * relief * smoothstep(84.0, 100.0, h) *
         smoothstep(0.18, 0.55, cont);

    // ---- Deep oceans ----------------------------------------------------
    // The floor sinks PROGRESSIVELY from the shore (cont ~ 0, land ~ 1)
    // out towards the middle of the ocean (cont very negative, land ~ 0):
    // it is shallow right at the beach and gets steadily deeper the
    // further out you sail, instead of being one flat shelf. `offshore` is
    // 0 at the coast and ramps to 1 in open water, and the drop is capped
    // so the deepest trench floor stays comfortably above bedrock.
    // It only engages where the base height is ALREADY under the sea
    // (cont below ~-0.20), so it deepens the ocean instead of pushing the
    // coastal shelf under water and flooding the land into ocean.
    const double offshore = smoothstep(-0.20, -0.45, cont) * (1.0 - land);
    h -= offshore * 20.0;

    // ---- Plains variant: a raised table ---------------------------------
    // A few blocks above the ordinary plain and flat on top. It is applied
    // AFTER the relief layers so the table keeps the flatness of the plain
    // it was cut out of, and BEFORE the river carve so a river still cuts
    // straight through it instead of running around it.
    h += 3.2 * plainsPlateau(n, wx, wz);

    // ---- Savanna canyons ------------------------------------------------
    // A broad table ~25 blocks above the savanna plain (measured: ~20-25 in
    // game). The gate is steep, so the step from the plain up onto the table
    // is a canyon wall; the top itself inherits the savanna's damped relief
    // (1-2 block steps), and the river carve right below this runs through
    // it.
    h += 24.0 * savannaCanyon(n, wx, wz);

    // ---- Rivers ---------------------------------------------------------
    // Three stages, on purpose. A WIDE valley lowers the land into a gentle
    // bowl, a still wider SKIRT eases the approach, and only then does the
    // NARROW river mask cut a thin channel into the bottom of that bowl.
    // Blending straight to a fixed deep bed (what this used to do) is what
    // produced slot canyons with near-vertical walls through the mountains
    // instead of the soft, wide valleys vanilla has.
    const double rv = riverValley(n, wx, wz) * land;
    if (rv > 0.0) {
        // Depth scales with how high the ground stands above sea level, so
        // a hill becomes a valley the river runs along — but it is CAPPED,
        // so even a tall mountain gets a broad valley rather than a gorge.
        //
        // The valley may lower the land TOWARD the water but never INTO it:
        // only the narrow channel mask below is allowed to cut the bed under
        // sea level. Without this clamp the valley floor of flat ground ends
        // up under the water line and the entire valley fills up — the wide
        // rivers the transect check kept catching on some seeds.
        const double above = std::max(0.0, h - (double)kSeaLevel);
        const double depth = std::min(std::min(5.0 + above * 0.40, 13.0),
                                      std::max(0.0, h - (double)(kSeaLevel + 1)));
        h -= rv * rv * (3.0 - 2.0 * rv) * depth;
    }

    // The wide, shallow skirt: same idea, spread much further out so the
    // ground eases down to the valley instead of dropping off it.
    const double rs = riverShoulder(n, wx, wz) * land;
    if (rs > 0.0) {
        // Flat ground barely dips. A shoulder that shaved several blocks off
        // EVERY nearby column, over an area this wide, pushed whole regions
        // below the water line and inflated the ocean from ~38% to ~41%.
        // The skirt only really matters where there is height to ease away
        // from.
        const double above = std::max(0.0, h - (double)kSeaLevel);
        const double depth = std::min(std::min(0.8 + above * 0.38, 9.0),
                                      std::max(0.0, h - (double)(kSeaLevel + 1)));
        h -= rs * rs * (3.0 - 2.0 * rs) * depth;
    }

    const double rm = riverMask(n, wx, wz) * land;
    if (rm > 0.0) {
        // Bed a few blocks under the water surface so the channel always
        // holds water at exactly kSeaLevel. min() guarantees the cut only
        // ever lowers the ground — blending to a fixed bed used to RAISE
        // low ground back up and leave the river dry.
        const double bed = (double)kSeaLevel - 4.0;
        h += (std::min(h, bed) - h) * rm;
    }

    return std::clamp(h, 10.0, 122.0);
}

int heightFromRaw(const double raw[3][3]) {
    // (1,2,1) * (1,2,1) / 16 — the two separable blur passes of the
    // original generator, collapsed into the 3x3 kernel it is equivalent
    // to. Keeping this mild keeps the terrain rough but removes the
    // 1-block terracing noise would otherwise show.
    // The old {1,2,1}/16 kernel put only 25% of the weight on the centre
    // sample, which flattened almost every local feature: hills read as
    // ramps and the biggest step between two neighbouring columns was 2.
    // {1,4,1}/36 keeps the same shape (it is still separable and still
    // removes 1-block terracing) while letting the short-wavelength relief
    // survive into the final height.
    static const double k[3] = {1.0, 4.0, 1.0};
    double sum = 0.0;
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
            sum += k[a] * k[b] * raw[a][b];
    int h = (int)std::lround(sum / 36.0);
    return std::clamp(h, 8, 124);
}

int columnHeight(const TerrainNoise& n, double wx, double wz) {
    double raw[3][3];
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
            raw[a][b] = rawColumnHeight(n, wx + a - 1.0, wz + b - 1.0);
    return heightFromRaw(raw);
}

namespace {

// Shared river field: the zero contour of a low-frequency fbm. Both the
// narrow channel mask and the wide valley mask read the SAME field, so the
// water always sits at the bottom of the valley that was carved for it.
//
// Domain warping is what makes the rivers SERPENTINE. On its own the zero
// contour of a smooth field is already a single connected network (every
// reach of {field == 0} either joins another reach at a saddle or runs out
// to the sea), but at this low frequency each reach runs almost perfectly
// straight for hundreds of blocks, which is why the rivers read as a few
// long canals instead of rivers that wander and merge with each other.
// Displacing the sampling point by a slow independent noise field first
// bends every reach without ever disconnecting one from the next.
//
// Four octaves: the extra detail keeps the contour from flattening into a
// straight line at any scale and breaks up the long flat stretches the
// field could have right at zero. On a flat spot the zero contour is
// ill-defined, the block distance blows up or collapses and the river used
// to vanish or flood the whole flat.
inline double riverField(const TerrainNoise& n, double wx, double wz) {
    // Vanilla-Java meandering: the warp that bends the reach is a SINGLE
    // very low-frequency octave. Two octaves (what this used to be) add a
    // shorter wavelength on top, which shows up as small back-and-forth
    // wiggles instead of the long, lazy S-curves a Java river makes. One
    // broad octave (~1700-block wavelength) bends each reach smoothly and
    // keeps the network connected.
    constexpr double kWarpFreq = 0.00060;   // ~1700-block wavelength
    constexpr double kAmp      = 30.0;      // blocks of sideways displacement
    const double ox = mcp_simplex_fbm2d(n.surface, wx * kWarpFreq + 12000.0,
                                        wz * kWarpFreq - 3100.0, 1, 0.5, 2.0);
    const double oz = mcp_simplex_fbm2d(n.surface, wx * kWarpFreq - 7300.0,
                                        wz * kWarpFreq + 9100.0, 1, 0.5, 2.0);
    return mcp_simplex_fbm2d(n.river,
                             (wx + ox * kAmp) * 0.0014,
                             (wz + oz * kAmp) * 0.0014, 4, 0.5, 2.0);
}

// Distance in BLOCKS from the river's zero contour, estimated as
// |field| / |gradient|.
//
// Measuring the channel in raw field units (|field| < kEdge) is what made
// rivers read as a string of wide ponds: wherever the field flattens out a
// tiny |field| covers tens of blocks, so the "narrow" threshold opened up
// into a lake. Dividing by the gradient turns the threshold into a real
// distance, so the water keeps the same width whether the field is steep or
// almost flat and a flat spot can no longer inflate the river.
inline double riverDistance(const TerrainNoise& n, double wx, double wz) {
    const double v  = riverField(n, wx, wz);
    const double gx = riverField(n, wx + 1.0, wz) - riverField(n, wx - 1.0, wz);
    const double gz = riverField(n, wx, wz + 1.0) - riverField(n, wx, wz - 1.0);
    const double g  = 0.5 * std::sqrt(gx * gx + gz * gz);   // field per block
    if (g < 1e-9) return 1e9;                               // no contour here
    return std::fabs(v) / g;
}

} // namespace

double riverMask(const TerrainNoise& n, double wx, double wz) {
    // Rivers are the zero contour of the low-frequency field, so the water
    // is the set of columns within a couple of blocks of that contour.
    const double d = riverDistance(n, wx, wz);
    // Measured in BLOCKS now (see riverDistance). The player asked for a
    // real river between 6 and 8 blocks across, not a ditch. kCore is the
    // radius that is carved at FULL depth (so 2*kCore + 1 columns are
    // guaranteed to hold water), and kEdge is where the bank ends. With
    // kCore ~2.8 the guaranteed channel is ~6-7 blocks, and the softer
    // partial-carve fringe pushes the measured run to ~7-8 — right in the
    // requested range.
    constexpr double kCore = 2.9;   // fully carved channel floor
    constexpr double kEdge = 3.2;   // river bank
    if (d >= kEdge) return 0.0;
    if (d <= kCore) return 1.0;
    const double t = (d - kCore) / (kEdge - kCore);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

double riverValley(const TerrainNoise& n, double wx, double wz) {
    const double d = std::fabs(riverField(n, wx, wz));
    // Much wider than riverMask(): this is the valley the river runs along,
    // and its edges fade out over a long distance so the banks slope
    // instead of dropping off a cliff. Note this is still the raw FIELD
    // magnitude, not the block distance: the valley is supposed to be a
    // broad, soft bowl, and the terrain carve below is clamped so it can
    // never dig a bowl deep enough to fill with water.
    constexpr double kValley = 0.40;
    if (d >= kValley) return 0.0;
    return 1.0 - smoothstep(0.0, kValley, d);
}

double riverShoulder(const TerrainNoise& n, double wx, double wz) {
    // A very wide, shallow skirt around the valley. One wide valley on its
    // own was measured to make the banks STEEPER, not gentler: widening the
    // mask just moved the same drop further out, so the step from valley
    // floor to untouched ground stayed abrupt. Adding this second, much
    // broader and shallower tier on top spreads the remaining drop over a
    // long distance, which is what actually flattens the transition.
    const double d = std::fabs(riverField(n, wx, wz));
    constexpr double kShoulder = 0.78;
    if (d >= kShoulder) return 0.0;
    return 1.0 - smoothstep(0.0, kShoulder, d);
}

bool isShore(const TerrainNoise& n, double wx, double wz, int h, int radius) {
    // radius defaults to 2 (see header): a 2-3 block wide band at the water
    // line, which is what a vanilla beach actually is.
    //
    // Only land within a couple of blocks of the water line can be a beach.
    // The window is deliberately tight: a wide one turns every low-lying
    // plain into a beach, because in flat coastal country plenty of ground
    // sits at that height. Vanilla beaches are a narrow band right at the
    // water's edge.
    //
    // This is also the cheap-guard: the expensive neighbour sampling below
    // only runs for the handful of columns that are actually near water.
    if (h < kSeaLevel - 1 || h > kSeaLevel + 2) return false;
    // Neighbour test uses the RAW height rather than the smoothed
    // columnHeight(): this predicate runs for every near-water column
    // (and again for every tree candidate), and columnHeight() costs nine
    // raw evaluations each. Asking "is this column below the water line?"
    // does not need the blur — a half-block margin absorbs it, and the
    // result is identical for every practical purpose at nine times the
    // speed.
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            if (dx == 0 && dz == 0) continue;
            if (rawColumnHeight(n, wx + dx, wz + dz) < (double)kSeaLevel - 0.5)
                return true;
        }
    }
    return false;
}

bool isOceanShore(const TerrainNoise& n, double wx, double wz, int h, int radius) {
    if (h < kSeaLevel - 1 || h > kSeaLevel + 2) return false;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            if (dx == 0 && dz == 0) continue;
            if (rawColumnHeight(n, wx + dx, wz + dz) >= (double)kSeaLevel - 0.5)
                continue;   // that neighbour is not water
            // The water we touch has to be OCEAN. A river channel is also
            // water at the same level, but a sandy bank is the NORM along
            // the sea and only an occasional accident along a river — and
            // this test is what tells the two apart.
            if (riverMask(n, wx + dx, wz + dz) < 0.15) return true;
        }
    }
    return false;
}

double riverSandBank(const TerrainNoise& n, double wx, double wz) {
    // Rare but LONG sand banks along rivers.
    //
    // Two things decide whether a stretch of bank is sand, and they have to
    // be kept separate. Which REACH of river is sandy comes from a very low
    // frequency field (~350-block wavelength) — that is what makes the sand
    // run in long stretches along the river instead of dropping round
    // patches every few blocks. The valley mask then decides WHERE on the
    // bank it is allowed to appear.
    //
    // The valley is used as a smooth GATE rather than multiplied in as a
    // weight. Multiplying by the raw valley made the result fall off towards
    // the top of the bank, so the column only cleared the threshold in the
    // few blocks right at the water line and the measured stretches came out
    // 4 blocks long. Ramping it to 1 well inside the valley instead keeps the
    // whole bank of a chosen reach at full strength.
    const double v = riverValley(n, wx, wz);
    if (v <= 0.06) return 0.0;
    const double nearRiver = smoothstep(0.06, 0.20, v);

    const double s = mcp_simplex_fbm2d(n.surface, wx * 0.0028 + 8080.0,
                                       wz * 0.0028 + 4040.0, 2, 0.5, 2.0);
    constexpr double kStart = 0.34;
    constexpr double kEdge  = 0.66;
    if (s <= kStart) return 0.0;
    const double t = (s - kStart) / (kEdge - kStart);
    return t * t * (3.0 - 2.0 * t) * nearRiver;
}

double gravelPatch(const TerrainNoise& n, double wx, double wz, int h) {
    // Rare, clustered patches. Vanilla lays gravel on riverbeds, on
    // coastlines and on the odd inland patch; the water-proximity term
    // keeps most of them where you would actually expect gravel. `h` is
    // passed in rather than recomputed, since this runs per land column.
    const double g = mcp_simplex_fbm2d(n.surface, wx * 0.035 + 61.0,
                                       wz * 0.035 - 17.0, 2, 0.5, 2.0);
    const double nearWater = 1.0 - smoothstep(0.0, 7.0,
                                              std::fabs((double)h - (double)kSeaLevel));
    const double near = std::max(riverValley(n, wx, wz),
                                 std::max(riverMask(n, wx, wz), nearWater));
    // Threshold tuned so patches are genuinely occasional rather than a
    // second biome painted over the first.
    constexpr double kEdge = 0.70;
    if (g <= 0.46) return 0.0;
    const double t = (g - 0.46) / (kEdge - 0.46);
    return std::clamp(t * t * (3.0 - 2.0 * t) * (0.35 + 0.65 * near), 0.0, 1.0);
}

double treeFreeMask(const TerrainNoise& n, double wx, double wz, int h) {
    // Rare open ground: a low-frequency field thresholded hard, so most of
    // the world keeps its full tree cover and only occasional pockets come
    // out bare.
    const double f = mcp_simplex_fbm2d(n.surface, wx * 0.012 - 421.0,
                                       wz * 0.012 + 88.0, 2, 0.5, 2.0);
    if (f <= 0.58) return 0.0;
    const double t = std::clamp((f - 0.58) / 0.16, 0.0, 1.0);
    // Biased TOWARDS river banks and lowland (which is what plains mostly
    // are): the clearing is likeliest right next to the water, not out in
    // the middle of a forest far from it.
    // `h` is passed in by the caller, which has already computed it.
    const double lowland = 1.0 - smoothstep(4.0, 22.0,
                                            (double)h - (double)kSeaLevel);
    const double bias = std::min(1.0, 0.45 + 0.55 * std::max(riverValley(n, wx, wz),
                                                             lowland * 0.6));
    return t * t * (3.0 - 2.0 * t) * bias;
}

uint8_t columnBiome(const TerrainNoise& n, double wx, double wz, int h) {
    using namespace biome;

    const double t  = tempAt(n, wx, wz);
    const double hu = humidAt(n, wx, wz);

    // River channel FIRST: a carved channel drops to kSeaLevel - 4, which is
    // below the ocean cut, so testing the ocean first would label the whole
    // river as OCEAN (the channel used to be mislabelled that way, which is
    // why the river looked like an ocean trench). The height band keeps the
    // river tag on the shallow channel bed only: deeper water is real ocean.
    const double rm = riverMask(n, wx, wz);
    if (rm > 0.25 && h < kSeaLevel && h >= kSeaLevel - 6)
        return (t < -0.25) ? BIOME_FROZEN_RIVER : BIOME_RIVER;

    // Below sea level minus a shallow margin: open ocean.
    if (h < kSeaLevel - 2) return BIOME_OCEAN;

    // Beach: only along the OCEAN. A river bank is normally grass, so
    // giving river columns BIOME_BEACH (whose top block is sand) is what
    // used to make every river run between two sandy strips. Uses the same
    // isOceanShore() the surface fill uses, so the biome and the sand on
    // the ground can never disagree.
    if (isOceanShore(n, wx, wz, h)) return BIOME_BEACH;

    // Where the terrain has actually been flattened into open grassland,
    // the biome has to agree with the shape of the ground, otherwise you
    // get forests and jungles standing on open plains and vice versa — and
    // the plains themselves end up full of trees the terrain was flattened
    // to make room for. The two variants read the SAME masks the terrain
    // uses, so the raised table and the woodland patches line up exactly.
    if (plainsFactor(n, wx, wz) > 0.55 && h < kSeaLevel + 16) {
        if (plainsForest(n, wx, wz) > 0.55) return BIOME_FOREST;
        if (plainsPlateau(n, wx, wz) > 0.55) return BIOME_PLAINS_PLATEAU;
        return BIOME_PLAINS;
    }

    // Savanna: the low-relief plains and, inside their core, the raised
    // canyon tables. The height cap is generous because the canyon table
    // itself stands ~22 blocks above the plain.
    const double sf = savannaFactor(n, wx, wz);
    if (sf > 0.55 && h < kSeaLevel + 45) {
        if (savannaCanyon(n, wx, wz) > 0.45) return BIOME_SAVANNA_PLATEAU;
        return BIOME_SAVANNA;
    }

    uint8_t b = BiomeRegistry::select(t, hu, h, kSeaLevel);
    if (b == BIOME_OCEAN) b = BIOME_PLAINS;
    return b;
}

} // namespace cursestone::level::generator

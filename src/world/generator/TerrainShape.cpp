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
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "world/generator/TerrainShape.h"
#include "world/generator/biome/BiomeRegistry.h"
#include "world/generator/biome/Biome.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>

namespace cursestone::world::generator {

namespace {

// ---- Memo de funciones puras por hilo (OPT, generacion lenta) -----------
//
// Generar un chunk pide las MISMAS columnas muchas veces: la rejilla
// 18x18 del relieve, luego isOceanShore() vuelve a evaluar hasta 24
// vecinos por columna (y el propio generate() lo hace otra vez para la
// arena de playa), columnHeight() evalua 9 celdas por candidato de arbol,
// riverDistance() son 5 evaluaciones del campo de rio con sus warps… y el
// rescan 3x3 del poblador de arboles repite todo el vecindario otra vez.
//
// Todas estas funciones son PURAS: mismo TerrainNoise + mismas
// coordenadas = mismos bits de salida. Memoizarlas por tanto NO puede
// cambiar el mundo: se guarda exactamente el doble que devolvio el
// calculo y se devuelve tal cual. thread_local porque cada worker del
// AsyncPool genera chunks en paralelo sobre el MISMO TerrainNoise (solo
// lectura) — sin candados y sin carreras. Direct-mapped: una colision
// solo pisa una entrada y cuesta recalcular, nunca da un valor viejo
// (la clave incluye el uid de la instancia, que cambia si el
// TerrainNoise se destruye y otro nace en su direccion).
struct MemoEntry {
    uint64_t uid = 0;
    uint64_t kx = 0, kz = 0, kw = 0;
    bool     valid = false;
    double   dv = 0.0;   // valor para funciones double
    int64_t  iv = 0;     // valor para funciones int/uint8
};

template <size_t N>
struct MemoTable { MemoEntry e[N]; };

inline uint64_t memoBits(double d) {
    uint64_t u;
    std::memcpy(&u, &d, sizeof u);
    return u;
}

// Mezcla tipo splitmix64. La version ingenua (multiply+xor-shift) daba el
// MISMO indice para todas las claves: los doubles enteros pequenos tienen
// los 32 bits bajos a cero (el entero vive en la parte ALTA de la
// mantisa), asi que kx*const y kz*const eran 0 en los bits que formaban
// el indice y TODAS las columnas mapeaban al mismo slot — la memo daba 0
// hits. Los finalizers multiplico-desplazamiento si suben los bits
// altos al bajo, que es exactamente lo que hace falta aqui.
inline uint64_t memoMix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

inline uint64_t memoHash(uint64_t uid, uint64_t kx, uint64_t kz, uint64_t kw) {
    return memoMix(kx ^ memoMix(kz + 0x9E3779B97F4A7C15ULL)
                       ^ memoMix(uid + kw * 0xC2B2AE3D27D4EB4FULL));
}

template <size_t N>
inline MemoEntry* memoFind(MemoTable<N>& t, uint64_t uid,
                           uint64_t kx, uint64_t kz, uint64_t kw) {
    MemoEntry& e = t.e[memoHash(uid, kx, kz, kw) & (N - 1)];
    return (e.valid && e.uid == uid && e.kx == kx && e.kz == kz && e.kw == kw)
               ? &e : nullptr;
}

template <size_t N>
inline MemoEntry& memoSlot(MemoTable<N>& t, uint64_t uid,
                           uint64_t kx, uint64_t kz, uint64_t kw) {
    MemoEntry& e = t.e[memoHash(uid, kx, kz, kw) & (N - 1)];
    e.uid = uid; e.kx = kx; e.kz = kz; e.kw = kw; e.valid = true;
    return e;
}

// Tablas por funcion y por hilo (cada una ~64-128 KB).
inline thread_local MemoTable<4096> t_rawMemo;      // rawColumnHeight
inline thread_local MemoTable<4096> t_colHMemo;     // columnHeight
inline thread_local MemoTable<4096> t_riverDMemo;   // riverDistance
inline thread_local MemoTable<4096> t_biomeMemo;    // columnBiome

// Contador de instancias de TerrainNoise (invalidacion de las tablas).
inline uint64_t nextTerrainNoiseUid() {
    static std::atomic<uint64_t> c{1};
    return c.fetch_add(1, std::memory_order_relaxed);
}

// ---- Puente entre el ruido vanilla y nuestro generador -------------------
//
// El generador pedia un fbm 2D normalizado a [-1, 1], con la octava BASE en
// la coordenada que le pasan:
//
//     fbm(x, y, octaves) = sum(noise(x*2^i, y*2^i) * 0.5^i) / sum(0.5^i)
//
// El PerlinNoise vanilla suma las octavas AL REVES — la frecuencia se divide y
// la amplitud se multiplica:
//
//     vanilla(x, y) = sum(noise(x/2^i, y/2^i) * 2^i)          (pow /= 2)
//
// asi que su salida NO es comparable: sin escalar, la octava dominante (la
// ultima) tiene periodo 2^(levels-1) veces mas largo que la base, y a la
// escala de bloque del generador el campo sale casi constante (mundo plano).
//
// El puente hace las dos cosas que hacen falta para que el campo signifique lo
// mismo que el fbm de antes:
//
//   1. escala la entrada por 2^(levels-1), de modo que la octava dominante
//      vuelve a caer exactamente en la frecuencia base que el generador venia
//      usando (y las demas quedan como detalle cada vez mas fino, cada vez mas
//      debil, como en un fbm);
//   2. divide por la amplitud total sum(2^i) = 2^levels - 1, que deja el
//      resultado acotado en [-1, 1].
//
// El parametro `octaves` del sitio que llama ya no decide el numero de octavas
// — eso lo fija la capa vanilla — pero se conserva en la firma para que cada
// punto de uso se siga leyendo igual (y porque cada capa SI tiene su propia
// cuenta de octavas, ver TerrainNoise).
// El valor CRUDO del campo: ya escalado y normalizado a [-1, 1] por la
// amplitud total, pero SIN la calibracion por mundo (offset/gain). El puente
// vive en noise/VanillaNoise.h porque el generador del Nether usa el mismo.
inline double rawField2d(const FieldNoise& f, double x, double y) {
    return noiseValue2d(f.layer, x, y);
}

inline double noiseFbm2d(const FieldNoise& f, double x, double y,
                         int /*octaves*/, double /*persistence*/,
                         double /*lacunarity*/) {
    return (rawField2d(f, x, y) - f.offset) * f.gain;
}

// ---- Calibracion por mundo ----------------------------------------------
// El campo vanilla, a la escala de bloque del generador, sale en buena parte
// constante en su banda baja (ver el comentario largo mas arriba): la media
// del trozo del ruido donde cae el mundo no es 0 y su relieve no es el del
// fbm anterior. `calibrate` mide las dos cosas en una zona de referencia y
// deja el campo con la MISMA media (0) y la MISMA desviacion que tenia el
// ruido al que reemplaza, de modo que cada umbral y cada mascara del
// generador siguen significando lo que significaban.
//
// Se hace una vez por mundo (y por campo), en el constructor de TerrainNoise.
constexpr double kCalibHalfSize = 512.0;  // zona de referencia: +/-512 bloques
constexpr double kCalibStep = 16.0;       //  64x64 muestras
constexpr double kMinGain = 0.25;
constexpr double kMaxGain = 24.0;

// `debias`: quitar la media medida. Solo vale para un campo que se evalua
// SIEMPRE a la misma escala (el clima y la red de rios). Un campo que se lee
// a escalas muy distintas (el relieve, la superficie) tiene una media
// distinta en cada una — a escala fina el trozo de ruido ya cubre varias
// octavas y su media es ~0 — asi que restar la media de una escala estropea
// las otras. Para esos campos se deja la media quieta (es pequena) y solo se
// ajusta la amplitud.
void calibrate(FieldNoise& f, double scale, double targetRms, bool debias) {
    double sum = 0.0, sumSq = 0.0;
    long n = 0;
    for (double x = -kCalibHalfSize; x < kCalibHalfSize; x += kCalibStep) {
        for (double z = -kCalibHalfSize; z < kCalibHalfSize; z += kCalibStep) {
            const double v = rawField2d(f, x * scale, z * scale);
            sum += v;
            sumSq += v * v;
            ++n;
        }
    }
    if (n == 0) return;
    const double mean = sum / (double)n;
    double var = sumSq / (double)n - mean * mean;
    if (var < 0.0) var = 0.0;
    const double rms = std::sqrt(var);
    f.offset = debias ? mean : 0.0;
    const double centred = debias ? std::sqrt(std::max(0.0, var)) : rms;
    f.gain = (centred > 1e-9) ? (targetRms / centred) : 1.0;
    if (f.gain < kMinGain) f.gain = kMinGain;
    if (f.gain > kMaxGain) f.gain = kMaxGain;
}

inline double smoothstep(double e0, double e1, double x) {
    double t = (x - e0) / (e1 - e0);
    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;
    return t * t * (3.0 - 2.0 * t);
}

// Land mask: 0 over deep ocean, 1 on the continent.
inline double landMask(double cont) { return smoothstep(-0.28, 0.02, cont); }

// ---- River cross-section -----------------------------------------------
// Ground height of a river as a function of the BLOCK distance `d` to the
// centre line of the channel. Four tiers, and together they are what gives
// a river its read from the bank:
//   * a flat bed, well under the water surface,
//   * a short lip climbing to the water line,
//   * a flat grass bank AT the water line, 2-3 blocks wide,
//   * then a steady climb that terraces into the surrounding land.
//
// The river used to be cut with a mask that dropped straight from untouched
// ground to a fixed bed, which is why every reach ended in a sheer vertical
// wall ("tan caido") with no bank and no steps. A smooth distance profile
// cannot produce that: the ground only ever descends 1.1 blocks per block
// outwards, so the smoothing kernel turns it into the stepped dirt/stone
// banks the reference rivers have.
constexpr double kRiverBed   = 2.4;   // half width carved to full depth
constexpr double kRiverEdge  = 3.2;   // where the bed reaches sea level
constexpr double kRiverShore = 3.0;   // flat grass bank at water level
constexpr double kRiverClimb = 1.2;   // lowland: gentle terraced banks
constexpr double kCliffClimb = 5.0;   // eroded gorge wall, ~65-75 deg once smoothed
// How hard the river network is pulled towards the jungle belt, as a
// fraction of the river field's own range. Roughly this many blocks of
// sideways deflection across the gradient — big enough to visibly steer the
// network, small enough that no reach is ever torn in two.
constexpr double kRiverJungleBias = 0.24;

inline double riverProfile(double d, double climb, double width) {
    const double bed   = (double)kSeaLevel - 4.0;
    const double kBed  = kRiverBed * width;
    const double kEdge = kRiverEdge * width;
    const double kShr  = kRiverShore * width;
    if (d <= kBed) return bed;
    if (d <= kEdge) {
        const double t = (d - kBed) / (kEdge - kBed);
        return bed + ((double)kSeaLevel - bed) * t;
    }
    // Flat bank at the water line, then the climb. The flat tier has to be an
    // explicit clamp, not just the start of the climb: without it the
    // profile keeps rising straight through sea level and the "bank" ends
    // up under the water instead of being a flat shelf beside it.
    if (d <= kEdge + kShr) return (double)kSeaLevel;
    // Capped so a mountain cannot be cut into a bottomless trench; past the
    // cap the broad valley terms take over.
    const double dc = std::min(d, kEdge + kShr + 40.0);
    return (double)kSeaLevel + (dc - kEdge - kShr) * climb;
}

// How steeply a river bank climbs. `cliff` is the erosion regime of the
// reach, in [0,1] — see where it is computed in rawColumnHeight.
//
// This is what separates a lowland river from a high-country one. Down in
// the plains a river meanders across a gentle slope, so its banks stay
// softly terraced. High up — a savanna plateau, a mountain — the same water
// has to fall the same height over a much shorter run, and it cuts a CLIFF:
// the gradient steepens towards 80 degrees, which is what an eroded gorge
// through high ground actually looks like.
//
// kCliffClimb sits above the raw tangent on purpose. The (1,4,1) smoothing
// kernel averages every column with its eight neighbours, so a raw wall
// rising R blocks per column comes out as roughly R/2.4 — there is a factor
// between this constant and the slope you actually walk up. Measured on the
// carved world: 17.0 came out at a median 76-82 degree wall, which was too
// sheer; 5.0 measures a median near 70 degrees, inside the 65-75 band that
// was asked for. The tail above the band is the mountain's OWN relief, not
// the river profile — the lowland band sits at 63 degrees on the same
// measurement.
inline double riverClimbRate(double cliff) {
    return kRiverClimb + cliff * (kCliffClimb - kRiverClimb);
}

// How wide this reach of river is, as a scale on kRiverBed/kRiverEdge.
//
// riverDistance() divides the river field by its gradient precisely so the
// water keeps a constant width whether the field is steep or nearly flat —
// that is what stops a flat spot from inflating the river into a lake. But
// it is also exactly why every single reach came out the same width, which
// is not how a river behaves: real ones run narrow and quick in one stretch
// and broad and slow in the next. This slow field (roughly 900-block
// wavelength) restores that variation without reintroducing the ponds,
// because it scales a distance threshold rather than a field threshold.
inline double riverWidthScale(const TerrainNoise& n, double wx, double wz) {
    const double w = noiseFbm2d(n.surface, wx * 0.0011 + 61000.0,
                                       wz * 0.0011 - 27000.0, 2, 0.5, 2.0);
    return 0.70 + 0.60 * (w * 0.5 + 0.5);   // 0.70 .. 1.30
}

// Half width of the water at this column. riverMask, the terrain carve and
// the biome choice all read this, so they can never disagree about how wide
// the river is here.
inline double riverHalfWidth(const TerrainNoise& n, double wx, double wz) {
    return kRiverEdge * riverWidthScale(n, wx, wz);
}

} // namespace

double continentField(const TerrainNoise& n, double wx, double wz) {
    return noiseFbm2d(n.height, wx * 0.0010, wz * 0.0010, 3, 0.5, 2.0);
}

double plainsFactor(const TerrainNoise& n, double wx, double wz) {
    const double cont = continentField(n, wx, wz);
    // Two terms. A biome-scale patch noise so plains also occur well inland,
    // and a coastal band keyed off `cont` so that the shelf of land just
    // above the ocean is almost always plains. The coastal term is what
    // puts the wide open grassland right against the sea, with hills and
    // forest further back — the shape the reference generator produces.
    const double p = noiseFbm2d(n.surface, wx * 0.0017 + 3300.0,
                                       wz * 0.0017 - 1700.0, 2, 0.5, 2.0);
    const double inland = smoothstep(0.02, 0.40, p);
    const double coast = smoothstep(-0.12, 0.10, cont) *
                         (1.0 - smoothstep(0.14, 0.38, cont));
    return std::clamp(std::max(inland * 0.85, coast * 0.95), 0.0, 1.0);
}

double tempAt(const TerrainNoise& n, double wx, double wz) {
    return noiseFbm2d(n.temp, wx * 0.0004, wz * 0.0004, 2, 0.5, 2.0);
}

double humidAt(const TerrainNoise& n, double wx, double wz) {
    return noiseFbm2d(n.humid, wx * 0.0005 + 512.0,
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
        noiseFbm2d(n.surface, wx * 0.0060 + 9100.0,
                          wz * 0.0060 - 2700.0, 2, 0.5, 2.0));
    if (patch <= 0.0) return 0.0;
    return patch * smoothstep(0.45, 0.72, plainsFactor(n, wx, wz));
}

double plainsForest(const TerrainNoise& n, double wx, double wz) {
    // Woodland-scale patches inside the grassland. Same idea as the
    // plateau: the biome selection turns this into BIOME_FOREST, so the
    // patch gets real forest density instead of a handful of stray oaks.
    const double patch = smoothstep(0.20, 0.48,
        noiseFbm2d(n.surface, wx * 0.0062 - 6100.0,
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
        noiseFbm2d(n.surface, wx * 0.0024 + 4200.0,
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
        noiseFbm2d(n.surface, wx * 0.0016 + 15500.0,
                          wz * 0.0016 + 7300.0, 2, 0.5, 2.0));
    // Near the desert the table is wanted on essentially every savanna
    // column, so the patch mask is lifted towards 1 there.
    if (desert > 0.0) patch = std::max(patch, desert);
    return core * interior * notPlains * patch;
}

TerrainNoise::TerrainNoise(int64_t seed)
    : vanilla(seed),
      // Cada campo del generador sobre una capa del juego completo vanilla.
      // El reparto es fijo: la capa "baja" A para el relieve principal, las
      // de 4 octavas para el clima (temperatura/humedad), la "baja" B para la
      // red de rios y forestNoise para la superficie sin arboles.
      height{vanilla.lperlinNoise1, 1.0, 0.0},
      temp{vanilla.perlinNoise2, 1.0, 0.0},
      humid{vanilla.perlinNoise3, 1.0, 0.0},
      river{vanilla.lperlinNoise2, 1.0, 0.0},
      surface{vanilla.forestNoise, 1.0, 0.0} {
    // Identidad para invalidar las memo-tablas por hilo (ver arriba).
    uid = nextTerrainNoiseUid();
    // Zona de referencia y desviacion objetivo por campo: son las que medía
    // el ruido anterior (simplex) sobre +/-512 bloques, que es el patron con
    // el que se ajustaron los umbrales del generador.
    calibrate(height, 0.0022, 0.3191, /*debias=*/false);
    calibrate(temp, 0.0004, 0.5456, /*debias=*/true);
    calibrate(humid, 0.0005, 0.3974, /*debias=*/true);
    calibrate(river, 0.0011, 0.2958, /*debias=*/true);
    calibrate(surface, 0.0017, 0.3443, /*debias=*/false);
}

// Las capas viven dentro de `vanilla`; no hay nada que liberar a mano.
TerrainNoise::~TerrainNoise() = default;

// Implementacion SIN memo (ver rawColumnHeight abajo, que es la que
// llama todo el codigo). No tocar la firma: la memo solo envuelve.
static double rawColumnHeightUncached(const TerrainNoise& n, double wx, double wz) {
    // Continent shape (oceans vs. land). Long wavelength.
    const double cont = continentField(n, wx, wz);
    // Broad rolling swells — the large-scale swell that makes hills read as
    // hills instead of as one uniform bump field.
    const double broad = noiseFbm2d(n.height, wx * 0.0022 + 313.0,
                                           wz * 0.0022 + 727.0, 3, 0.5, 2.0);
    // Rolled hills.
    const double hill = noiseFbm2d(n.height, wx * 0.0055 + 77.3,
                                          wz * 0.0055 - 41.7, 4, 0.5, 2.0);
    // Small surface detail — this is what keeps the terrain from looking
    // like a smooth ramp.
    const double det = noiseFbm2d(n.height, wx * 0.0140 + 900.0,
                                         wz * 0.0140 + 300.0, 3, 0.5, 2.0);
    // Fine crags. The long-wavelength layers barely move between two
    // neighbouring columns, so without a short-wavelength layer the ground
    // reads as glassy smooth no matter how big the hills are. This is what
    // actually gives the surface its local roughness.
    const double crag = noiseFbm2d(n.height, wx * 0.0320 - 555.0,
                                          wz * 0.0320 + 111.0, 2, 0.5, 2.0);
    // Ridged mountains: a value that folds at 0 into sharp crests.
    const double ridgeN = noiseFbm2d(n.height, wx * 0.0032 + 1234.5,
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
    const double outN = noiseFbm2d(n.height, wx * 0.0210 + 88.0,
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
    // A smooth depth ramp on its own reads as one giant bowl — a smooth
    // hill under the water — which is not how a sea floor looks. Vanilla
    // floors are procedural: seamounts and ridges rising out of the deep,
    // saddles and trenches between them, and a general roughness at every
    // scale. Both terms below are weighted by `offshore`, so the coast keeps
    // its smooth shelf and only the genuinely deep water gets relief.
    if (offshore > 0.0) {
        const double floorN = noiseFbm2d(n.height, wx * 0.0048 + 2200.0,
                                                wz * 0.0048 - 6100.0, 3, 0.5, 2.0);
        const double ridgeIn = noiseFbm2d(n.height, wx * 0.0021 - 4400.0,
                                                wz * 0.0021 + 1700.0, 2, 0.5, 2.0);
        double seamount = 1.0 - std::fabs(ridgeIn);
        seamount = seamount * seamount;
        h += offshore * (floorN * 7.0 + (seamount - 0.28) * 13.0);
    }

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
    // The height the land stands at before ANY river term runs. The gorge
    // steepness below is read from this, not from the height the valley has
    // already dug, so the whole gorge shares one cliff angle.
    const double hLand = h;
    // Erosion regime of this reach, in [0,1]: 0 where a river meanders across
    // a gentle lowland slope, 1 where it has to cut through high ground.
    // Read from the height the land stands at BEFORE any river term runs, so
    // a whole gorge agrees on its regime — deriving it from the post-valley
    // height instead made the cliff rate depend on how much the valley had
    // already eaten, which varies from column to column and tore the gorge
    // into steps.
    const double cliff = smoothstep((double)kSeaLevel + 8.0,
                                    (double)kSeaLevel + 28.0, hLand);
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
        // EROSION: in high country the same water cuts a narrow GORGE
        // rather than laying out a broad valley, so the valley is damped out
        // as `cliff` rises. This is what actually makes the cliff appear —
        // with the valley left at full strength it pre-lowered the ground
        // across the whole width of the gorge, and the cliff profile below
        // then had almost nothing left to cut, which measured as a gentle
        // slope no matter how steep the profile constant was.
        h -= rv * rv * (3.0 - 2.0 * rv) * depth * (1.0 - 0.85 * cliff);
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
        h -= rs * rs * (3.0 - 2.0 * rs) * depth * (1.0 - 0.85 * cliff);
    }

    // ---- River channel ---------------------------------------------------
    // A smooth cross-section of the distance to the centre line (see
    // riverProfile) instead of a mask. min() guarantees the cut only ever
    // LOWERS the ground — blending to a fixed bed used to RAISE low ground
    // back up and leave the river dry — and because the profile descends
    // gently the banks come out as steps rather than as a sheer drop.
    if (land > 0.02) {
        const double fade = std::min(1.0, land / 0.30);
        const double target =
            riverProfile(riverDistance(n, wx, wz), riverClimbRate(cliff),
                         riverWidthScale(n, wx, wz));
        h -= std::max(0.0, h - target) * fade;
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

static int columnHeightUncached(const TerrainNoise& n, double wx, double wz) {
    double raw[3][3];
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
            raw[a][b] = rawColumnHeight(n, wx + a - 1.0, wz + b - 1.0);
    return heightFromRaw(raw);
}

// ---- Wrappers memoizados (OPT) -------------------------------------------
// Devuelven EXACTAMENTE lo que devolverian las *Uncached: la memo solo
// recuerda el resultado de la misma entrada (misma instancia + mismas
// coordenadas enteras). Con coordenadas no enteras (nadie lo hace hoy,
// pero por robustez) se calcula directo sin tocar la tabla.

double rawColumnHeight(const TerrainNoise& n, double wx, double wz) {
    if (wx != std::floor(wx) || wz != std::floor(wz))
        return rawColumnHeightUncached(n, wx, wz);
    const uint64_t kx = memoBits(wx), kz = memoBits(wz);
    if (const MemoEntry* e = memoFind(t_rawMemo, n.uid, kx, kz, 0)) return e->dv;
    const double v = rawColumnHeightUncached(n, wx, wz);
    memoSlot(t_rawMemo, n.uid, kx, kz, 0).dv = v;
    return v;
}

int columnHeight(const TerrainNoise& n, double wx, double wz) {
    if (wx != std::floor(wx) || wz != std::floor(wz))
        return columnHeightUncached(n, wx, wz);
    const uint64_t kx = memoBits(wx), kz = memoBits(wz);
    if (const MemoEntry* e = memoFind(t_colHMemo, n.uid, kx, kz, 0)) return (int)e->iv;
    const int v = columnHeightUncached(n, wx, wz);
    memoSlot(t_colHMemo, n.uid, kx, kz, 0).iv = v;
    return v;
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
inline double riverField(const TerrainNoise& n, double wx, double wz, double bias) {
    // Irregular, crooked meandering. One very broad octave (~1700-block
    // wavelength) gives the reach its overall sweep, and a second octave at
    // 45% amplitude on a 2.8x shorter wavelength bends it the other way
    // often enough to give the reach kinks and short reaches of different
    // length instead of one long smooth drawn arc.
    constexpr double kWarpFreq = 0.00060;
    constexpr double kAmp      = 30.0;      // blocks of sideways displacement
    const double ox = noiseFbm2d(n.surface, wx * kWarpFreq + 12000.0,
                                        wz * kWarpFreq - 3100.0, 2, 0.45, 2.8);
    const double oz = noiseFbm2d(n.surface, wx * kWarpFreq - 7300.0,
                                        wz * kWarpFreq + 9100.0, 2, 0.45, 2.8);
    const double f = noiseFbm2d(n.river,
                                       (wx + ox * kAmp) * 0.0014,
                                       (wz + oz * kAmp) * 0.0014, 4, 0.5, 2.0);
    // Subtracting the bias moves the zero contour towards the jungle belt.
    return f - bias;
}

// How strongly this column is pulled towards the jungle belt, as a fraction
// of the river field's own range. Rivers are wanted to run mostly through
// jungle, and the way to do that without wrecking the network is to DEFLECT
// the existing contour rather than to draw new rivers: the climate fields
// change over thousands of blocks, far more slowly than the river field
// itself, so adding a small smooth term shifts each reach towards the wet
// hot country while leaving every reach connected. The contour sits where
// field == bias, so subtracting the bias walks it into the jungle.
inline double riverJungleBias(const TerrainNoise& n, double wx, double wz) {
    // One octave each: this is a very slow field, so the extra octave the
    // public tempAt()/humidAt() use buys nothing here and this runs for
    // every column of the world.
    const double t  = noiseFbm2d(n.temp,   wx * 0.0004, wz * 0.0004, 1, 0.5, 2.0);
    const double hu = noiseFbm2d(n.humid,  wx * 0.0005, wz * 0.0005, 1, 0.5, 2.0);
    // The jungle band of BiomeRegistry::select(): hot AND genuinely wet.
    const double lush = smoothstep(0.34, 0.60, t) * smoothstep(0.18, 0.45, hu);
    return kRiverJungleBias * lush;
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
} // namespace

static double riverDistanceUncached(const TerrainNoise& n, double wx, double wz) {
    const double bias = riverJungleBias(n, wx, wz);
    const double v  = riverField(n, wx, wz, bias);
    const double gx = riverField(n, wx + 1.0, wz, bias) - riverField(n, wx - 1.0, wz, bias);
    const double gz = riverField(n, wx, wz + 1.0, bias) - riverField(n, wx, wz - 1.0, bias);
    const double g  = 0.5 * std::sqrt(gx * gx + gz * gz);   // field per block
    if (g < 1e-9) return 1e9;                               // no contour here
    return std::fabs(v) / g;
}

double riverDistance(const TerrainNoise& n, double wx, double wz) {
    // Memo (OPT): cada columna pide la distancia al rio varias veces
    // (mascara, bioma, orilla…), y cada evaluacion son 5 campos de rio
    // con sus warps — de las cosas mas caras por columna.
    if (wx != std::floor(wx) || wz != std::floor(wz))
        return riverDistanceUncached(n, wx, wz);
    const uint64_t kx = memoBits(wx), kz = memoBits(wz);
    if (const MemoEntry* e = memoFind(t_riverDMemo, n.uid, kx, kz, 0)) return e->dv;
    const double v = riverDistanceUncached(n, wx, wz);
    memoSlot(t_riverDMemo, n.uid, kx, kz, 0).dv = v;
    return v;
}

double riverMask(const TerrainNoise& n, double wx, double wz) {
    // Rivers are the zero contour of the low-frequency field, so the water
    // is the set of columns within a couple of blocks of that contour.
    const double d = riverDistance(n, wx, wz);
    // Scaled by the same width field the carve uses (riverWidthScale), so
    // "is there a river here" and "what does the river carve here" can never
    // disagree — including on a reach where the river runs narrow.
    const double width = riverWidthScale(n, wx, wz);
    const double kCore = kRiverBed * width;
    const double kEdge = kRiverEdge * width;
    if (d >= kEdge) return 0.0;
    if (d <= kCore) return 1.0;
    const double t = (d - kCore) / (kEdge - kCore);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

double riverValley(const TerrainNoise& n, double wx, double wz) {
    const double d = std::fabs(riverField(n, wx, wz, riverJungleBias(n, wx, wz)));
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
    const double bias = riverJungleBias(n, wx, wz);
    // A very wide, shallow skirt around the valley. One wide valley on its
    // own was measured to make the banks STEEPER, not gentler: widening the
    // mask just moved the same drop further out, so the step from valley
    // floor to untouched ground stayed abrupt. Adding this second, much
    // broader and shallower tier on top spreads the remaining drop over a
    // long distance, which is what actually flattens the transition.
    const double d = std::fabs(riverField(n, wx, wz, bias));
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
            // The water we touch has to be OCEAN, not a river channel. Both are
            // water at the same level, but a sandy bank is the NORM along
            // the sea and only an occasional accident along a river, so
            // this test is what tells the two apart — and getting it wrong
            // is what painted every river bank as a sand beach. The water
            // has to be a good way clear of the channel profile before it
            // counts as sea.
            if (riverDistance(n, wx + dx, wz + dz) > 8.0) return true;
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

    const double s = noiseFbm2d(n.surface, wx * 0.0028 + 8080.0,
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
    const double g = noiseFbm2d(n.surface, wx * 0.035 + 61.0,
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
    const double f = noiseFbm2d(n.surface, wx * 0.012 - 421.0,
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

static uint8_t columnBiomeUncached(const TerrainNoise& n, double wx, double wz, int h) {
    using namespace biome;

    const double t  = tempAt(n, wx, wz);
    const double hu = humidAt(n, wx, wz);

    // River channel FIRST: a carved channel drops to kSeaLevel - 4, which is
    // below the ocean cut, so testing the ocean first would label the whole
    // river as OCEAN (the channel used to be mislabelled that way, which is
    // why the river looked like an ocean trench). The test asks the same
    // channel profile the terrain carve uses, so the biome, the water and
    // the bank all agree on where the river ends. The height band keeps the
    // river tag on the channel bed only: deeper water is real ocean.
    const double rd = riverDistance(n, wx, wz);
    if (rd <= riverHalfWidth(n, wx, wz) + 1.6 && h < kSeaLevel && h >= kSeaLevel - 6)
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

uint8_t columnBiome(const TerrainNoise& n, double wx, double wz, int h) {
    // Memo (OPT): el bioma de una columna se pregunta muchas veces con el
    // MISMO h (Normal::generate, el poblador de arboles con su h del
    // chunk, el de estructuras…), y calcularlo repite temperatura,
    // humedad, distancia al rio y los tests de orilla/sabana. La clave
    // incluye h: si alguien pregunta el mismo punto con otra altura, no
    // puede recibir el bioma cacheado de la otra.
    if (wx != std::floor(wx) || wz != std::floor(wz))
        return columnBiomeUncached(n, wx, wz, h);
    const uint64_t kx = memoBits(wx), kz = memoBits(wz);
    const uint64_t kh = (uint64_t)(uint32_t)h;
    if (const MemoEntry* e = memoFind(t_biomeMemo, n.uid, kx, kz, kh))
        return (uint8_t)e->iv;
    const uint8_t v = columnBiomeUncached(n, wx, wz, h);
    memoSlot(t_biomeMemo, n.uid, kx, kz, kh).iv = v;
    return v;
}

} // namespace cursestone::world::generator

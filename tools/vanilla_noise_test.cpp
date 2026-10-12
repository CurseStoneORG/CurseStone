// tools/vanilla_noise_test.cpp
//
// Prueba de oro del ruido vanilla.
//
// Los numeros de abajo NO se inventaron: salen de compilar el propio
// SSMGAlt/PocketEdition (handheld/src/world/level/levelgen/synth +
// util/Random.h) y volcar sus valores para la semilla 1337. Como el port tiene
// el mismo algoritmo y el mismo orden de construccion, tiene que dar
// EXACTAMENTE lo mismo:
//
//   * los hashCode() de las 8 capas cubren la tabla de permutacion p[512] y
//     los offsets xo/yo/zo, asi que un solo byte de diferencia en el
//     MersenneRandom o en el barajado los cambia,
//   * los getValue() cubren la interpolacion, el gradiente y la suma de
//     octavas.
//
//   g++ -std=c++20 -Isrc -o /tmp/vanilla_noise_test tools/vanilla_noise_test.cpp \
//       src/world/generator/noise/*.cpp
//   /tmp/vanilla_noise_test
#include "world/generator/noise/VanillaNoise.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace cursestone::world::generator;

namespace {

int failures = 0;

void check(bool ok, const std::string& name) {
    printf("  [%s] %s\n", ok ? " OK " : "FAIL", name.c_str());
    if (!ok) ++failures;
}

// El ruido es float y sus valores van de ~0.15 a ~12000, asi que la
// comparacion es relativa.
bool close(float got, double want) {
    const double g = (double)got;
    const double scale = std::fabs(want) > 1.0 ? std::fabs(want) : 1.0;
    return std::fabs(g - want) <= 1e-6 * scale;
}

void checkValue(float got, double want, const std::string& name) {
    const bool ok = close(got, want);
    printf("  [%s] %-28s got=%.9g want=%.9g\n", ok ? " OK " : "FAIL",
           name.c_str(), (double)got, want);
    if (!ok) ++failures;
}

} // namespace

int main() {
    // Referencia: semilla 1337, capas tal como las arma RandomLevelSource.
    VanillaNoise vn(1337);

    // 1. Los hashCode cubren p[512] (la permutacion sacada del MT19937) y el
    //    orden de construccion de las ocho capas.
    check(vn.lperlinNoise1.hashCode() == 292755495, "lperlinNoise1 hashCode");
    check(vn.lperlinNoise2.hashCode() == -1490727921, "lperlinNoise2 hashCode");
    check(vn.perlinNoise1.hashCode() == -1141448369, "perlinNoise1 hashCode");
    check(vn.perlinNoise2.hashCode() == 647389487, "perlinNoise2 hashCode");
    check(vn.perlinNoise3.hashCode() == -1631528177, "perlinNoise3 hashCode");
    check(vn.scaleNoise.hashCode() == -93632553, "scaleNoise hashCode");
    check(vn.depthNoise.hashCode() == -816767041, "depthNoise hashCode");
    check(vn.forestNoise.hashCode() == -1400350529, "forestNoise hashCode");

    check(vn.lperlinNoise1.levelCount() == 16, "lperlinNoise1 tiene 16 octavas");
    check(vn.perlinNoise1.levelCount() == 8, "perlinNoise1 tiene 8 octavas");
    check(vn.perlinNoise2.levelCount() == 4, "perlinNoise2 tiene 4 octavas");
    check(vn.scaleNoise.levelCount() == 10, "scaleNoise tiene 10 octavas");

    // 2. Los valores: mismos numeros que el original en los tres puntos que
    //    se volcaron del reference (ver la cabecera).
    struct Sample { float x, y, z; };
    const Sample s[3] = {{0.0f, 0.0f, 0.5f}, {0.5f, 0.125f, 0.5f}, {1.25f, -0.5f, 0.5f}};
    const double want[3][9] = {
        { -3717.10986, -8351.37207, 9.0248003, 0.150094628, 6.2622118,
          -23.0173798, -12542.3271, -28.0935001, 10.7459688 },
        { -3717.10986, -8351.37109, 9.02339077, 0.150904655, 6.26123905,
          -23.0175171, -12542.3262, -28.0946121, 10.7403374 },
        { -3717.11108, -8351.37109, 9.02204227, 0.152946353, 6.2600193,
          -23.0178909, -12542.3262, -28.0946655, 10.7252207 },
    };

    for (int i = 0; i < 3; ++i) {
        const float x = s[i].x * 0.001f, y = s[i].y * 0.001f;
        const std::string tag = "muestra " + std::to_string(i) + " ";
        checkValue(vn.lperlinNoise1.getValue(x, y), want[i][0], tag + "lperlinNoise1");
        checkValue(vn.lperlinNoise2.getValue(x, y), want[i][1], tag + "lperlinNoise2");
        checkValue(vn.perlinNoise1.getValue(x, y), want[i][2], tag + "perlinNoise1");
        checkValue(vn.perlinNoise2.getValue(x, y), want[i][3], tag + "perlinNoise2");
        checkValue(vn.perlinNoise3.getValue(x, y), want[i][4], tag + "perlinNoise3");
        checkValue(vn.scaleNoise.getValue(x, y), want[i][5], tag + "scaleNoise");
        checkValue(vn.depthNoise.getValue(x, y), want[i][6], tag + "depthNoise");
        checkValue(vn.forestNoise.getValue(x, y), want[i][7], tag + "forestNoise");
        checkValue(vn.perlinNoise1.getValue(s[i].x * 0.01f, s[i].y * 0.01f, s[i].z),
                   want[i][8], tag + "perlinNoise1 3D");
    }

    // 3. Determinista: la misma semilla da el mismo mundo.
    VanillaNoise again(1337);
    check(again.perlinNoise1.hashCode() == vn.perlinNoise1.hashCode(),
          "misma semilla -> misma permutacion");
    check(again.depthNoise.getValue(0.001f, 0.002f) ==
              vn.depthNoise.getValue(0.001f, 0.002f),
          "misma semilla -> mismos valores");
    VanillaNoise other(1338);
    check(other.perlinNoise1.hashCode() != vn.perlinNoise1.hashCode(),
          "semilla distinta -> otra permutacion");

    printf("\n%s (%d failure(s))\n",
           failures ? "VANILLA NOISE TEST: FAIL" : "VANILLA NOISE TEST: OK", failures);
    return failures ? 1 : 0;
}

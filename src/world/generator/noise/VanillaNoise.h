// SPDX-License-Identifier: LGPL-3.0-or-later
//
// VanillaNoise — el juego COMPLETO de ruido vanilla.
//
// Son las ocho capas de PerlinNoise que RandomLevelSource construye, con
// sus mismos niveles y en el MISMO orden de construccion:
//
//   lperlinNoise1(&random, 16)   lperlinNoise2(&random, 16)
//   perlinNoise1 (&random,  8)   perlinNoise2 (&random,  4)
//   perlinNoise3 (&random,  4)   scaleNoise   (&random, 10)
//   depthNoise   (&random, 16)   forestNoise  (&random,  8)
//
// El orden importa: las ocho capas se alimentan del MISMO MersenneRandom, asi
// que cada una consume su parte de la secuencia. Cambiar el orden (o los
// niveles de una) desplaza el resto y el mundo deja de coincidir con el
// vanilla. Esta es la parte que no se puede improvisar.
#pragma once

#include "world/generator/noise/MersenneRandom.h"
#include "world/generator/noise/PerlinNoise.h"

namespace cursestone::world::generator {

class VanillaNoise {
public:
    explicit VanillaNoise(int64_t seed) : random((long)seed),
        lperlinNoise1(&random, 16),
        lperlinNoise2(&random, 16),
        perlinNoise1(&random, 8),
        perlinNoise2(&random, 4),
        perlinNoise3(&random, 4),
        scaleNoise(&random, 10),
        depthNoise(&random, 16),
        forestNoise(&random, 8) {}

    // Tiene que ir primero: todas las capas toman &random.
    MersenneRandom random;

    PerlinNoise lperlinNoise1;  // ruido "bajo" A (continentes/hondonadas)
    PerlinNoise lperlinNoise2;  // ruido "bajo" B (relleno del medio)
    PerlinNoise perlinNoise1;   // ruido selector principal (8 octavas)
    PerlinNoise perlinNoise2;   // clima
    PerlinNoise perlinNoise3;   // clima
    PerlinNoise scaleNoise;     // escala del relieve
    PerlinNoise depthNoise;     // profundidad vertical
    PerlinNoise forestNoise;    // densidad de vegetacion
};

// ============================================================
// Puente del ruido vanilla a un valor [-1, 1]
// ============================================================
// El PerlinNoise vanilla suma las octavas AL REVES que un fbm normal:
//
//     vanilla(x, y) = sum(noise(x/2^i, y/2^i) * 2^i)      (pow /= 2)
//     fbm(x, y)     = sum(noise(x*2^i, y*2^i) * 0.5^i)
//
// asi que su octava dominante (la ultima) cae 2^(levels-1) veces mas lejos
// que la frecuencia que le pasan, y a la escala de bloque del generador el
// campo sale casi constante. Estas funciones devuelven la octava dominante a
// la frecuencia base (multiplicando la entrada por 2^(levels-1)) y dividen por
// la amplitud total sum(2^i) = 2^levels - 1, dejando un valor acotado en
// [-1, 1] comparable al del fbm que usaba el generador.
inline double layerAmplitudeSum(int levels) {
    double s = 0.0, amp = 1.0;
    for (int i = 0; i < levels; ++i) { s += amp; amp *= 2.0; }
    return s;
}

inline double layerDominantScale(int levels) {
    return (levels <= 0) ? 1.0 : (double)(1u << (levels - 1));
}

inline double noiseValue2d(PerlinNoise& p, double x, double y) {
    // Las dos constantes ya vienen precalculadas en la capa (init());
    // antes se recalculaban con un bucle en CADA evaluacion de campo, y
    // cada columna de terreno pide decenas de estas. Mismos valores,
    // mismos bits, sin el bucle.
    const double sum = p.amplitudeSum();
    if (sum <= 0.0) return 0.0;
    const double k = p.dominantScale();
    return (double)p.getValue((float)(x * k), (float)(y * k)) / sum;
}

inline double noiseValue3d(PerlinNoise& p, double x, double y, double z) {
    const double sum = p.amplitudeSum();
    if (sum <= 0.0) return 0.0;
    const double k = p.dominantScale();
    return (double)p.getValue((float)(x * k), (float)(y * k), (float)(z * k)) / sum;
}

} // namespace cursestone::world::generator

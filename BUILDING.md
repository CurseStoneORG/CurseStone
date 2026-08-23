# Cómo compilar CenisysPE

## Opción 1 — GitHub Actions (recomendado)

Sube el repo a GitHub y el workflow `.github/workflows/build.yml` compila automáticamente:
- **Linux x86_64** — para probar en VPS/PC
- **Android ARM64** — para correr en Android/Termux

Los binarios aparecen en **Actions → tu workflow → Artifacts**.

Para hacer un release con los binarios adjuntos:
```bash
git tag v1.0.0
git push origin v1.0.0
```

---

## Opción 2 — Linux x86_64 (VPS / PC)

```bash
sudo apt-get install cmake ninja-build gcc-12 g++-12 zlib1g-dev libleveldb-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/CenisysPE
```

---

## Opción 3 — Android ARM64 desde Termux (en tu celular)

```bash
# Instalar dependencias en Termux
pkg update && pkg install -y \
  cmake ninja clang \
  zlib leveldb

# Compilar
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_STANDARD=23 \
  -DCMAKE_CXX_STANDARD=20

cmake --build build --parallel

# Correr
./build/CenisysPE
```

> Termux detecta automáticamente ARM64, no necesitas pasar el toolchain manualmente.

---

## Opción 4 — Cross-compile ARM64 desde Linux (con NDK)

```bash
# Descargar NDK r26c
wget https://dl.google.com/android/repository/android-ndk-r26c-linux.zip
unzip android-ndk-r26c-linux.zip
export NDK=$PWD/android-ndk-r26c

cmake -B build-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-arm64 --parallel
# Copia el binario a tu Android y córrelo en Termux
```

---

## Variables CMake útiles

| Variable | Descripción |
|----------|-------------|
| `CMAKE_BUILD_TYPE` | `Release` o `Debug` |
| `ZLIB_ROOT` | Path a zlib si no lo detecta automáticamente |
| `LEVELDB_INCLUDE_DIRS` | Include dir de LevelDB |
| `LEVELDB_LIBRARIES` | Librería `.a` de LevelDB |

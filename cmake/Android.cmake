# cmake/Android.cmake — Android NDK cross-compile config
# Usado cuando -DCMAKE_TOOLCHAIN_FILE apunta aquí directamente,
# o heredado desde el toolchain oficial del NDK con -DANDROID=1.
#
# Para compilar manualmente en Termux:
#   cmake -B build \
#     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a \
#     -DANDROID_PLATFORM=android-24
#
# El workflow de GitHub Actions usa el toolchain oficial del NDK y pasa
# ZLIB_ROOT y LEVELDB_* apuntando a las librerías estáticas pre-compiladas.

# API mínima: Android 7.0 (API 24) — soporta C++20 con Clang del NDK r26+
if(NOT ANDROID_PLATFORM)
    set(ANDROID_PLATFORM "android-24")
endif()

# Static C++ runtime linking.
#
# IMPORTANT: The NDK ships Clang + libc++ (NOT libstdc++). The flag
# `-static-libstdc++` is a GCC-ism and is silently ignored by Clang,
# producing the warning:
#   clang++: warning: argument unused during compilation: '-static-libstdc++'
#
# The static-vs-dynamic decision is controlled by ANDROID_STL below:
#   c++_static  → libc++ is statically linked into the final binary
#   c++_shared  → libc++.so is bundled as a separate .so
#
# So there is nothing to add to CXX_FLAGS here. Setting ANDROID_STL=c++_static
# below already gives us a self-contained binary.

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}")

# STL: usa libc++ (la única opción en NDK r23+)
if(NOT ANDROID_STL)
    set(ANDROID_STL "c++_static")
endif()

message(STATUS "Android build — ABI: ${ANDROID_ABI}, Platform: ${ANDROID_PLATFORM}, STL: ${ANDROID_STL}")

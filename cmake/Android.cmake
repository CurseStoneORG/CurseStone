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

# Forzar runtime estático para que el binario no dependa de libstdc++.so
# (útil para distribuir el ejecutable sin instalar nada extra en Android)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}")

# STL: usa libc++ (la única opción en NDK r23+)
if(NOT ANDROID_STL)
    set(ANDROID_STL "c++_static")
endif()

message(STATUS "Android build — ABI: ${ANDROID_ABI}, Platform: ${ANDROID_PLATFORM}, STL: ${ANDROID_STL}")

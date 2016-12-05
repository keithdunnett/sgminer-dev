cmake_minimum_required (VERSION 3.5)

#include_directories(..)
add_library(sph bmw.c echo.c jh.c luffa.c gost.c simd.c blake.c cubehash.c groestl.c keccak.c shavite.c skein.c sha2.c sha2big.c fugue.c hamsi.c panama.c shabal.c whirlpool.c sha256_Y.c ripemd.c)
#target_include_directories(sph PUBLIC $CMAKE_CURRENT_SOURCE_DIR)

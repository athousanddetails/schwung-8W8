cmake_minimum_required(VERSION 3.15)
project(schwung-8w8 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
# No -ffast-math: it lets the compiler assume no NaN/Inf, which would remove
# the safety of the clamps on the realtime path. It would also break the null
# test — w8_ugens.h reproduces SuperCollider's arithmetic, and a compiler free
# to reassociate it does not reproduce anything.
set(CMAKE_C_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

set(W8_SOURCES
    src/dsp/w8_plugin.cpp
    src/dsp/w8_engine.cpp
)

# The Move module: dlopen'd by Schwung's chain host as dsp.so
add_library(w8 SHARED ${W8_SOURCES})
target_include_directories(w8 PRIVATE src src/dsp src/host)
target_link_libraries(w8 PRIVATE m)
set_target_properties(w8 PROPERTIES OUTPUT_NAME "dsp" PREFIX "" SUFFIX ".so")
# libstdc++ static: the Move ships 6.0.29 and we will not gamble on it.
target_link_options(w8 PRIVATE -static-libstdc++ -static-libgcc)

# End-to-end loader test: dlopens the real dsp.so like the chain host does.
# Cross-compiled and run ON the Move.
add_executable(w8_loadtest src/tools/loadtest.c)
target_include_directories(w8_loadtest PRIVATE src src/dsp src/host)
target_link_libraries(w8_loadtest PRIVATE dl m)

# CPU gate: renders each voice in isolation and reports realtime factor.
# 6W6's metal voices took 55% of the block budget before they were rewritten;
# this is here so 8W8 finds that out on the device rather than in a gig.
add_executable(w8_bench src/tools/bench.cpp src/dsp/w8_engine.cpp)
target_include_directories(w8_bench PRIVATE src src/dsp src/host)
target_link_libraries(w8_bench PRIVATE m)
target_link_options(w8_bench PRIVATE -static-libstdc++ -static-libgcc)

# Offline renderer: same engine, writes WAVs so the kit can be heard and A/B'd
# against hardware without a device in the loop.
add_executable(w8_render src/tools/render.cpp src/dsp/w8_engine.cpp)
target_include_directories(w8_render PRIVATE src src/dsp src/host)
target_link_libraries(w8_render PRIVATE m)
target_link_options(w8_render PRIVATE -static-libstdc++ -static-libgcc)

# The circuit kick, held to the paper's claims. Native only: it is a test, and
# it is the only verification that voice has — there is nothing to null it
# against. See tools/bd_check.cpp.
add_executable(w8_bd_check tools/bd_check.cpp)
target_include_directories(w8_bd_check PRIVATE src src/dsp)
target_link_libraries(w8_bd_check PRIVATE m)

# Kit balance: fits the per-lane trims. Native only — it is a design tool.
add_executable(w8_kit_check tools/kit_check.cpp src/dsp/w8_engine.cpp)
target_include_directories(w8_kit_check PRIVATE src src/dsp src/host)
target_link_libraries(w8_kit_check PRIVATE m)

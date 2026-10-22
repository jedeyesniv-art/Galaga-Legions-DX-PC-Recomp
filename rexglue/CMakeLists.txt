# galaga - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(galaga VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(GALAGA_SOURCES
    src/main.cpp
    src/game_files.cpp
    src/spin_hooks.cpp
    src/frame_stats.cpp
    src/profiler.cpp
    src/diagnostics.cpp
    src/steady_ui.cpp
)

if(WIN32)
    add_executable(galaga WIN32 ${GALAGA_SOURCES})
else()
    add_executable(galaga ${GALAGA_SOURCES})
endif()

rexglue_setup_target(galaga GPU_PLUGINS xenos)
target_compile_definitions(galaga PRIVATE GALAGA_VERSION="${PROJECT_VERSION}")

# CPU level for the recompiled game code. x86-64-v3 (AVX2 generation: Haswell / Zen and later, incl. the Steam
# Deck) brings MOVBE (the big-endian loads/stores everywhere), BMI2 (shifts/rotates) and hardware FMA (PowerPC
# fmadd via std::fma, exact either way). -ffp-contract=off keeps the compiler from fusing separate multiplies and
# adds on its own, so floating-point results stay those of the Xbox.
# Set GALAGA_CPU_LEVEL=x86-64-v2 for a build that runs on older CPUs.
set(GALAGA_CPU_LEVEL "x86-64-v3" CACHE STRING "-march level for the recompiled game code")
if(TARGET galaga_recomp)
    target_compile_options(galaga_recomp PRIVATE -march=${GALAGA_CPU_LEVEL} -ffp-contract=off)
endif()

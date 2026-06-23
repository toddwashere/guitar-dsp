include(FetchContent)

# ── WORLD (Morise, BSD-3) — formant-preserving vocoder library ───────────────
# Pinned to v1.0.1 (latest stable tag on github.com/mmorise/World as of 2026-06).
# v1.1.x tags referenced in the design doc do not exist; v1.0.1 is HEAD/master.
set(WORLD_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WORLD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    world
    GIT_REPOSITORY https://github.com/mmorise/World.git
    GIT_TAG        v1.0.1
)
FetchContent_GetProperties(world)
if(NOT world_POPULATED)
    FetchContent_Populate(world)
    # WORLD ships a CMakeLists at root that defines the `world` static-lib target.
    add_subdirectory(${world_SOURCE_DIR} ${world_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

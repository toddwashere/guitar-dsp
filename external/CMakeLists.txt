include(FetchContent)

# ── WORLD (Morise, BSD-3) — formant-preserving vocoder library ───────────────
# Pinned to v1.0.1 (latest stable tag on github.com/mmorise/World as of 2026-06).
# v1.1.x tags referenced in the design doc do not exist; v1.0.1 is HEAD/master.
set(WORLD_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WORLD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    world
    GIT_REPOSITORY https://github.com/mmorise/World.git
    GIT_TAG        d625e7608ca23a870018f01e7c562ac683d9847f  # SHA of v1.0.1 — pinned to prevent moving-tag drift
)
FetchContent_GetProperties(world)
if(NOT world_POPULATED)
    FetchContent_Populate(world)
    # WORLD ships a CMakeLists at root that defines the `world` static-lib target.
    add_subdirectory(${world_SOURCE_DIR} ${world_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# Silence WORLD's ~hundreds of unused-parameter / narrowing-conversion warnings.
# -w suppresses ALL warnings for this target only; our code's -Werror is unaffected.
if(TARGET world)
    target_compile_options(world PRIVATE -w)
endif()

# ---- ONNX Runtime (RAVE inference, scene 5) -----------------------------
set(ONNX_VERSION "1.18.0")
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/onnxruntime-osx-arm64-${ONNX_VERSION}.tgz")
    else()
        set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/onnxruntime-osx-x86_64-${ONNX_VERSION}.tgz")
    endif()
else()
    message(FATAL_ERROR "ONNX Runtime fetch only configured for macOS in this project")
endif()
FetchContent_Declare(onnxruntime_prebuilt URL "${ONNX_URL}")
FetchContent_MakeAvailable(onnxruntime_prebuilt)

add_library(onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${onnxruntime_prebuilt_SOURCE_DIR}/lib/libonnxruntime.dylib"
    INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_prebuilt_SOURCE_DIR}/include")
add_library(onnxruntime::onnxruntime ALIAS onnxruntime)

# Install the dylib next to the app/AU at build time
set(GUITAR_DSP_ONNX_DYLIB "${onnxruntime_prebuilt_SOURCE_DIR}/lib/libonnxruntime.dylib"
    CACHE INTERNAL "Path to the ONNX Runtime dylib for bundling")

cmake_minimum_required(VERSION 3.22)
project(guitar_dsp VERSION 0.1.0 LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# macOS deployment target — must support AVSpeechSynthesizer neural voices
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "macOS deployment target")

# Build the universal2 binary on macOS for portability
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

# Optional sanitizers for debug builds
option(GUITAR_DSP_ASAN "Build with AddressSanitizer" OFF)
option(GUITAR_DSP_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)

if(GUITAR_DSP_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(GUITAR_DSP_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

# Auto-fetch runtime assets that are gitignored (too large to commit).
# Set GUITAR_DSP_AUTO_FETCH_ASSETS=OFF to skip — useful in CI or when assets
# have been mounted from a cache.
option(GUITAR_DSP_AUTO_FETCH_ASSETS
       "Run scripts/fetch_*.sh during configure if required assets are missing" ON)

if(GUITAR_DSP_AUTO_FETCH_ASSETS)
    # Piper TTS binary + en_US-amy-medium voice (~65 MB total). Required for
    # scene 7 (Speaking — Piper word-by-word). The fetched tarball from
    # rhasspy/piper may currently be missing the .dylib runtime libs on macOS;
    # if scene 7 fails to speak, build the missing dylibs from source — see
    # docs/ for instructions.
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/piper/piper"
       OR NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/piper/voices/en_US-amy-medium.onnx")
        message(STATUS "Piper assets missing — running scripts/fetch_piper.sh")
        execute_process(
            COMMAND "${CMAKE_SOURCE_DIR}/scripts/fetch_piper.sh"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE _piper_fetch_rc)
        if(NOT _piper_fetch_rc EQUAL 0)
            message(WARNING "Piper fetch failed (exit ${_piper_fetch_rc}). "
                "Scene 7 (Speaking — Piper) won't work until you run "
                "scripts/fetch_piper.sh manually and resolve the cause.")
        endif()
    endif()

    # Whisper STT model (~150 MB). Required for the conversational-AI feature.
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/Resources/whisper/ggml-base.en.bin")
        message(STATUS "Whisper model missing — running scripts/fetch_whisper.sh")
        execute_process(
            COMMAND "${CMAKE_SOURCE_DIR}/scripts/fetch_whisper.sh"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE _whisper_fetch_rc)
        if(NOT _whisper_fetch_rc EQUAL 0)
            message(WARNING "Whisper fetch failed (exit ${_whisper_fetch_rc}). "
                "Conversational-AI STT won't work until you run "
                "scripts/fetch_whisper.sh manually.")
        endif()
    endif()
endif()

# Pull in FetchContent-managed externals (WORLD, etc.)
add_subdirectory(external)

# Pull in JUCE
add_subdirectory(external/JUCE)

# Pull in Catch2
add_subdirectory(external/Catch2)

# Pull in whisper.cpp (STT). Skip its examples + tests + server — we only need the lib.
# Disable native-arch optimisation: we build universal2 (arm64 + x86_64) so -march=native
# would pick the host arch and break the fat binary.
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE            OFF CACHE BOOL "" FORCE)
# Disable GPU backends — we need CPU transcription only, and the Metal backend
# has narrowing-conversion errors that break compilation with our Clang flags.
set(GGML_METAL             OFF CACHE BOOL "" FORCE)
set(GGML_BLAS              OFF CACHE BOOL "" FORCE)
add_subdirectory(external/whisper.cpp)

# Application source tree
add_subdirectory(src)

# Tools (fixture generators, offline test harnesses)
add_subdirectory(tools)

# Test tree
enable_testing()
add_subdirectory(tests)

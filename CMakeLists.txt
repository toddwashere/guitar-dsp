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

# Fixture generator tools
add_subdirectory(tools/fixtures)

# Test tree
enable_testing()
add_subdirectory(tests)

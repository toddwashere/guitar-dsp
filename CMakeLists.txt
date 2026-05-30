cmake_minimum_required(VERSION 3.22)
project(guitar_dsp VERSION 0.1.0 LANGUAGES CXX OBJCXX)

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

# Application source tree
add_subdirectory(src)

# Test tree
# add_subdirectory(tests)  # uncommented in Task 6 when tests/CMakeLists.txt is added
enable_testing()

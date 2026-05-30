# Audio module library — pure C++, no JUCE dependency.
# Modules added here as they are implemented in later tasks.
# Placeholder source keeps the empty static library legal under CMake/Ninja
# until the first real module (Task 11) lands.
add_library(guitar_dsp_audio STATIC
    audio/.placeholder.cpp
)

target_include_directories(guitar_dsp_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(guitar_dsp_audio PUBLIC cxx_std_20)

# Standalone application (declared in Task 5)
# The app target is added by app/CMakeLists.txt when it exists.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt")
    add_subdirectory(app)
endif()

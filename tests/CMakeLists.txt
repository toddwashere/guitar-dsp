add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    unit/test_synthetic_guitar.cpp
    unit/test_golden_file.cpp
    unit/audio/test_input_stage.cpp
    unit/audio/test_mixer.cpp
    unit/audio/test_audio_graph.cpp
    harness/RealtimeSentinel.cpp
    harness/SyntheticGuitar.cpp
    harness/GoldenFile.cpp
)

target_link_libraries(guitar_dsp_tests PRIVATE
    guitar_dsp_audio
    juce::juce_audio_formats
    Catch2::Catch2WithMain
)

target_include_directories(guitar_dsp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include(${CMAKE_SOURCE_DIR}/external/Catch2/extras/Catch.cmake)
catch_discover_tests(guitar_dsp_tests)

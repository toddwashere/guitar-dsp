add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    unit/test_synthetic_guitar.cpp
    unit/test_golden_file.cpp
    unit/audio/test_input_stage.cpp
    unit/audio/test_mixer.cpp
    unit/audio/test_audio_graph.cpp
    unit/audio/test_ivocoder_compiles.cpp
    unit/scenes/test_scene_compiles.cpp
    unit/scenes/test_scene_library.cpp
    unit/scenes/test_scene_engine.cpp
    unit/midi/test_fcb1010_mapping.cpp
    integration/test_passthrough_golden.cpp
    integration/test_realtime_safety.cpp
    integration/test_scene_switch.cpp
    harness/RealtimeSentinel.cpp
    harness/SyntheticGuitar.cpp
    harness/GoldenFile.cpp
)

target_link_libraries(guitar_dsp_tests PRIVATE
    guitar_dsp_audio
    guitar_dsp_scenes
    guitar_dsp_midi
    juce::juce_audio_formats
    Catch2::Catch2WithMain
)

target_include_directories(guitar_dsp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include(${CMAKE_SOURCE_DIR}/external/Catch2/extras/Catch.cmake)
catch_discover_tests(guitar_dsp_tests)

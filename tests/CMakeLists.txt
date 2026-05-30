add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    harness/RealtimeSentinel.cpp
)

target_link_libraries(guitar_dsp_tests PRIVATE
    guitar_dsp_audio
    Catch2::Catch2WithMain
)

target_include_directories(guitar_dsp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include(${CMAKE_SOURCE_DIR}/external/Catch2/extras/Catch.cmake)
catch_discover_tests(guitar_dsp_tests)

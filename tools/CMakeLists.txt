add_subdirectory(fixtures)

add_executable(shift_test shift_test.cpp)
target_link_libraries(shift_test PRIVATE world)
target_compile_features(shift_test PRIVATE cxx_std_17)

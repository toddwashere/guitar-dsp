add_library(guitar_dsp_ml STATIC
    RaveInference.cpp
)
target_include_directories(guitar_dsp_ml PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(guitar_dsp_ml PUBLIC
    onnxruntime::onnxruntime
    juce::juce_core
)
target_compile_features(guitar_dsp_ml PUBLIC cxx_std_20)

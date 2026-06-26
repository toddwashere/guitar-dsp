#include <catch2/catch_test_macros.hpp>
#include "ml/RaveInference.h"

#include <vector>

using guitar_dsp::ml::RaveInference;
using guitar_dsp::ml::RaveStatus;

namespace {
const char* stubModelPath() {
    return STUB_MODEL_PATH;  // injected via target_compile_definitions
}
}

TEST_CASE("RaveInference: loadModel on stub returns Loaded", "[ml][rave-inference]") {
    RaveInference inf;
    inf.loadModel(stubModelPath());
    REQUIRE(inf.status() == RaveStatus::Loaded);
}

TEST_CASE("RaveInference: loadModel on missing file returns Unavailable", "[ml][rave-inference]") {
    RaveInference inf;
    inf.loadModel("/nonexistent/path/missing.onnx");
    REQUIRE(inf.status() == RaveStatus::Unavailable);
    REQUIRE(!inf.lastError().empty());
}

TEST_CASE("RaveInference: process passthrough matches input", "[ml][rave-inference]") {
    RaveInference inf;
    inf.loadModel(stubModelPath());
    REQUIRE(inf.status() == RaveStatus::Loaded);

    std::vector<float> in(2048), out(2048, -999.0f);
    for (size_t i = 0; i < in.size(); ++i) in[i] = 0.001f * float(i);
    REQUIRE(inf.process(in.data(), out.data(), in.size()));
    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE(std::fabs(out[i] - in[i]) < 1e-6f);
    REQUIRE(inf.lastInferenceMs() >= 0.0f);
}

TEST_CASE("RaveInference: process when Unavailable returns false", "[ml][rave-inference]") {
    RaveInference inf;
    inf.loadModel("/nonexistent.onnx");
    std::vector<float> in(64), out(64);
    REQUIRE_FALSE(inf.process(in.data(), out.data(), in.size()));
}

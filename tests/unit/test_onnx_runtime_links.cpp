#include <catch2/catch_test_macros.hpp>
#include <onnxruntime_cxx_api.h>

TEST_CASE("ONNX Runtime: Env constructs without throwing", "[ml][onnx][smoke]") {
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "guitar_dsp_test");
    auto allocator = Ort::AllocatorWithDefaultOptions();
    (void)allocator;
    SUCCEED();
}

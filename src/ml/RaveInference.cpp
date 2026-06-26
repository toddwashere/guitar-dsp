#include "ml/RaveInference.h"
#include <onnxruntime_cxx_api.h>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>

namespace guitar_dsp::ml {

RaveInference::RaveInference() = default;
RaveInference::~RaveInference() = default;

void RaveInference::loadModel(const std::string& path) {
    status_.store(RaveStatus::Loading, std::memory_order_release);
    try {
        if (!std::filesystem::exists(path)) {
            std::lock_guard lk(errMx_);
            lastError_ = "model file not found: " + path;
            status_.store(RaveStatus::Unavailable, std::memory_order_release);
            return;
        }
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "guitar_dsp_rave");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_ = std::make_unique<Ort::Session>(*env_, path.c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        inputName_  = session_->GetInputNameAllocated(0, alloc).get();
        outputName_ = session_->GetOutputNameAllocated(0, alloc).get();
        status_.store(RaveStatus::Loaded, std::memory_order_release);
    } catch (const std::exception& e) {
        std::lock_guard lk(errMx_);
        lastError_ = e.what();
        status_.store(RaveStatus::Unavailable, std::memory_order_release);
    }
}

std::string RaveInference::lastError() const {
    std::lock_guard lk(errMx_);
    return lastError_;
}

bool RaveInference::process(const float* in, float* out, std::size_t n) noexcept {
    if (status_.load(std::memory_order_acquire) != RaveStatus::Loaded) return false;
    try {
        const auto t0 = std::chrono::steady_clock::now();

        std::array<int64_t, 2> shape{1, int64_t(n)};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            mem, const_cast<float*>(in), n, shape.data(), shape.size());

        const char* inputNames[]  = { inputName_.c_str() };
        const char* outputNames[] = { outputName_.c_str() };

        auto outputs = session_->Run(
            Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);
        if (outputs.empty() || !outputs[0].IsTensor()) return false;
        const float* src = outputs[0].GetTensorData<float>();
        std::memcpy(out, src, n * sizeof(float));

        const auto t1 = std::chrono::steady_clock::now();
        lastMs_.store(
            std::chrono::duration<float, std::milli>(t1 - t0).count(),
            std::memory_order_relaxed);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace guitar_dsp::ml

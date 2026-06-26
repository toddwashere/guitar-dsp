#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace Ort { class Env; class Session; }

namespace guitar_dsp::ml {

enum class RaveStatus { Loading, Loaded, Unavailable };

class RaveInference {
public:
    RaveInference();
    ~RaveInference();

    void loadModel(const std::string& onnxPath);
    RaveStatus status() const noexcept { return status_.load(std::memory_order_acquire); }
    std::string lastError() const;
    bool process(const float* in, float* out, std::size_t n) noexcept;
    float lastInferenceMs() const noexcept { return lastMs_.load(std::memory_order_relaxed); }

private:
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::string inputName_, outputName_;
    std::string lastError_;
    mutable std::mutex errMx_;
    std::atomic<RaveStatus> status_{RaveStatus::Loading};
    std::atomic<float> lastMs_{0.0f};
};

} // namespace guitar_dsp::ml

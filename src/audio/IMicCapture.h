#pragma once
#include <vector>

namespace guitar_dsp::audio {

class IMicCapture {
public:
    virtual ~IMicCapture() = default;
    virtual void beginCapture() = 0;
    virtual std::vector<float> endCapture() = 0;
    virtual bool  isCapturing()              const noexcept = 0;
    virtual float currentPeakDbfs()          const noexcept = 0;
    virtual bool  lastResultWasTooShort()    const noexcept = 0;
    virtual bool  lastResultWasTruncated()   const noexcept = 0;
};

} // namespace guitar_dsp::audio

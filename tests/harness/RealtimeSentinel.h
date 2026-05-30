#pragma once

#include <atomic>

namespace guitar_dsp::tests {

// Test-time sentinel for detecting heap allocations on the audio thread.
// Mark the current thread as realtime before exercising audio code; if any
// `operator new` / `operator delete` runs on that thread, the violation
// counter increments. Assert it stays at zero.
//
// Detection is process-wide because operator new is overridden globally in
// the test binary. Multiple RealtimeSentinel instances share state via the
// `violationCount()` static accessor.
class RealtimeSentinel {
public:
    RealtimeSentinel();
    ~RealtimeSentinel();

    void markCurrentThreadAsRealtime();
    void unmarkCurrentThreadAsRealtime();

    // Snapshot of violations recorded since this sentinel was constructed.
    std::size_t violations() const;

    // Process-wide hook used by overridden operator new/delete. Returns true
    // if the calling thread is currently marked as realtime.
    static bool isCurrentThreadRealtime();
    static void recordViolation();

private:
    std::size_t baseline_;
};

} // namespace guitar_dsp::tests

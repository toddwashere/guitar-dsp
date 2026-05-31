#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "TTSClip.h"

namespace guitar_dsp::audio {

class ITTSSource;

// Background worker that synthesizes ITTSSource clips off the message
// thread, caching results by key. Hides per-scene synthesis latency
// (~300 ms-1 s for live sources) so scene activation feels instant.
//
// Threading:
//   - Constructed on the message thread.
//   - enqueue() / waitForKey() / takeIfReady(): message thread.
//   - Internally spawns one background std::thread that calls
//     source.synthesize(). Source must be safe to call from a non-main
//     thread (AppleTTSSource is — AVSpeechSynthesizer is thread-safe).
//
// On destruction, the background thread is joined cleanly.
class TTSPrewarmer {
public:
    explicit TTSPrewarmer(ITTSSource& source);
    ~TTSPrewarmer();

    TTSPrewarmer(const TTSPrewarmer&) = delete;
    TTSPrewarmer& operator=(const TTSPrewarmer&) = delete;

    // Queue a key for background synthesis. Idempotent: if the key
    // is already queued, in-flight, or cached (success or failure),
    // this call is a no-op.
    void enqueue(const std::string& key);

    // Non-blocking lookup. Returns the cached clip if synthesis has
    // finished (nullptr if it finished but failed); returns nullptr
    // if synthesis is still pending or the key was never enqueued.
    // To distinguish "pending" from "missing", use isCached().
    TTSClipPtr takeIfReady(const std::string& key) const;

    // True if the key has a result cached (success OR failure).
    bool isCached(const std::string& key) const;

    // Blocking wait. Returns the clip (or nullptr on failure / timeout).
    // Call from the message thread when you must have the clip ready
    // and synthesis hasn't completed yet.
    TTSClipPtr waitForKey(const std::string& key,
                          std::chrono::milliseconds timeout);

private:
    ITTSSource& source_;

    mutable std::mutex      mu_;
    std::condition_variable cv_;

    std::deque<std::string>                       queue_;       // FIFO of keys to synth
    std::unordered_map<std::string, TTSClipPtr>   cache_;       // key -> result (may be nullptr)
    bool                                          shutdown_ = false;

    std::thread worker_;

    void workerMain();
};

} // namespace guitar_dsp::audio

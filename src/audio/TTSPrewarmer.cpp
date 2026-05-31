#include "TTSPrewarmer.h"

#include "ITTSSource.h"

namespace guitar_dsp::audio {

TTSPrewarmer::TTSPrewarmer(ITTSSource& source)
    : source_(source), worker_([this] { workerMain(); }) {}

TTSPrewarmer::~TTSPrewarmer() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        shutdown_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void TTSPrewarmer::enqueue(const std::string& key) {
    if (key.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (cache_.find(key) != cache_.end()) return;  // already done
        for (const auto& q : queue_) if (q == key) return;  // already queued
        queue_.push_back(key);
    }
    cv_.notify_one();
}

TTSClipPtr TTSPrewarmer::takeIfReady(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = cache_.find(key);
    return (it != cache_.end()) ? it->second : nullptr;
}

bool TTSPrewarmer::isCached(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mu_);
    return cache_.find(key) != cache_.end();
}

TTSClipPtr TTSPrewarmer::waitForKey(const std::string& key,
                                    std::chrono::milliseconds timeout) {
    enqueue(key);
    std::unique_lock<std::mutex> lock(mu_);
    const bool ok = cv_.wait_for(lock, timeout, [this, &key] {
        return shutdown_ || cache_.find(key) != cache_.end();
    });
    if (!ok) return nullptr;
    auto it = cache_.find(key);
    return (it != cache_.end()) ? it->second : nullptr;
}

void TTSPrewarmer::workerMain() {
    while (true) {
        std::string next;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return shutdown_ || !queue_.empty(); });
            if (shutdown_) return;
            next = std::move(queue_.front());
            queue_.pop_front();
        }
        // Synthesize off the lock — may be slow.
        TTSClipPtr result = source_.synthesize(next);

        {
            std::lock_guard<std::mutex> lock(mu_);
            cache_[next] = std::move(result);  // nullptr on failure is fine
        }
        cv_.notify_all();
    }
}

} // namespace guitar_dsp::audio

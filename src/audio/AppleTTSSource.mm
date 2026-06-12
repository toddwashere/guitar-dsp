#include "AppleTTSSource.h"

#import <AVFoundation/AVFoundation.h>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <vector>

namespace guitar_dsp::audio {

struct AppleTTSSource::Impl {
    AVSpeechSynthesizer* synth = nil;
};

AppleTTSSource::AppleTTSSource() : impl_(std::make_unique<Impl>()) {
    @autoreleasepool {
        impl_->synth = [[AVSpeechSynthesizer alloc] init];
    }
}

AppleTTSSource::~AppleTTSSource() = default;

void AppleTTSSource::prepare(double targetSampleRate) {
    targetSampleRate_ = targetSampleRate;
}

void AppleTTSSource::setVoice(std::string voiceIdentifier) {
    voiceIdentifier_ = std::move(voiceIdentifier);
}

TTSClipPtr AppleTTSSource::synthesize(const std::string& text) {
    if (text.empty()) return nullptr;

    auto clip = std::make_shared<TTSClip>();
    clip->name = text.substr(0, 32);  // first 32 chars for debugging
    clip->sampleRate = targetSampleRate_;

    // Synthesis state lives on the heap with SHARED ownership. The
    // writeUtterance:toBufferCallback: block can be delivered on the main
    // queue and may fire AFTER this function returns or times out (e.g. an
    // audio-device restart drains a backlogged main queue). The block
    // captures a shared_ptr by value, so the mutex/cv/buffer stay alive as
    // long as the block exists. Capturing pointers to stack locals here was
    // a use-after-free: a late callback would lock a destroyed mutex, and
    // std::mutex::lock() THROWS (EINVAL) rather than deadlocking, aborting
    // the whole app. (Cannot crash at all costs.)
    struct SynthState {
        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
        bool gotAnyAudio = false;
        double srcSampleRate = 0.0;
        std::vector<float> collected;
    };
    auto state = std::make_shared<SynthState>();

    @autoreleasepool {
        NSString* nsText = [NSString stringWithUTF8String:text.c_str()];
        AVSpeechUtterance* utterance = [AVSpeechUtterance speechUtteranceWithString:nsText];

        if (!voiceIdentifier_.empty()) {
            NSString* nsVoice = [NSString stringWithUTF8String:voiceIdentifier_.c_str()];
            AVSpeechSynthesisVoice* voice =
                [AVSpeechSynthesisVoice voiceWithIdentifier:nsVoice];
            if (voice) utterance.voice = voice;
        }

        // writeUtterance:toBufferCallback: invokes the callback repeatedly
        // with successive AVAudioPCMBuffer chunks, then once with a nil
        // (empty) buffer to signal end-of-synthesis. `state` is captured by
        // value (shared_ptr retain), so a late callback is always safe.
        [impl_->synth writeUtterance:utterance toBufferCallback:^(AVAudioBuffer* _Nonnull buffer) {
            std::lock_guard<std::mutex> lock(state->mu);

            AVAudioPCMBuffer* pcm = (AVAudioPCMBuffer*)buffer;
            const AVAudioFrameCount nFrames = pcm.frameLength;

            if (nFrames == 0) {
                state->done = true;
                state->cv.notify_one();
                return;
            }
            state->gotAnyAudio = true;

            // Remember the source sample rate (first chunk).
            if (state->srcSampleRate == 0.0) {
                state->srcSampleRate = pcm.format.sampleRate;
            }

            // AVSpeechSynthesizer typically returns Int16 mono (format may
            // vary across macOS versions). Handle Float32 and Int16; fall
            // back to skip for anything else.
            const AudioStreamBasicDescription& asbd = *pcm.format.streamDescription;
            const int channels = static_cast<int>(asbd.mChannelsPerFrame);
            const bool isFloat = (asbd.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
            const int bytesPerSample = static_cast<int>(asbd.mBitsPerChannel / 8);

            const std::size_t prevSize = state->collected.size();
            state->collected.resize(prevSize + nFrames);

            if (isFloat && bytesPerSample == 4) {
                const float* src = pcm.floatChannelData[0];
                for (AVAudioFrameCount i = 0; i < nFrames; ++i) {
                    float sum = src[i];
                    for (int c = 1; c < channels; ++c) sum += pcm.floatChannelData[c][i];
                    state->collected[prevSize + i] = sum / static_cast<float>(channels);
                }
            } else if (!isFloat && bytesPerSample == 2) {
                const int16_t* src = pcm.int16ChannelData[0];
                constexpr float invInt16 = 1.0f / 32768.0f;
                for (AVAudioFrameCount i = 0; i < nFrames; ++i) {
                    long sum = src[i * channels];
                    for (int c = 1; c < channels; ++c) sum += src[i * channels + c];
                    state->collected[prevSize + i] = static_cast<float>(sum) * invInt16
                                            / static_cast<float>(channels);
                }
            } else {
                // Unsupported format; drop the chunk (silent contribution).
                std::fill(state->collected.begin() + static_cast<long>(prevSize),
                          state->collected.end(), 0.0f);
            }
        }];

        // Wait for synthesis to finish (with a generous timeout). On timeout
        // we return nullptr; any callbacks that arrive later still find a
        // live `state` (held by the block) and are harmless no-ops.
        {
            std::unique_lock<std::mutex> lock(state->mu);
            // 3s cap: in a plugin host (esp. headless validators like auval),
            // Apple TTS can stall indefinitely because AVSpeechSynthesizer's
            // main-queue callbacks aren't being delivered. A short timeout
            // makes the chain fall back to prebaked fast instead of hanging
            // plugin load for 10s on every cold validation.
            const bool ok = state->cv.wait_for(lock, std::chrono::seconds(3),
                                               [&state] { return state->done; });
            if (!ok || !state->gotAnyAudio) {
                std::cerr << "[AppleTTSSource] synthesis failed or timed out for: "
                          << text.substr(0, 64) << '\n';
                return nullptr;
            }
        }

        // Synthesis is done (end-of-stream received) — no more callbacks will
        // mutate `state`, so we can read it without the lock.
        const double srcSampleRate = state->srcSampleRate;
        std::vector<float>& collected = state->collected;

        // Resample if needed (linear, same approach as PrebakedTTSSource).
        if (std::abs(srcSampleRate - targetSampleRate_) < 0.5) {
            clip->samples = std::move(collected);
        } else {
            const double ratio = srcSampleRate / targetSampleRate_;
            const std::size_t srcLen = collected.size();
            const std::size_t outLen = static_cast<std::size_t>(srcLen / ratio);
            clip->samples.resize(outLen);
            for (std::size_t i = 0; i < outLen; ++i) {
                const double srcIdx = i * ratio;
                const std::size_t i0 = static_cast<std::size_t>(srcIdx);
                const float frac = static_cast<float>(srcIdx - i0);
                const std::size_t i1 = std::min(i0 + 1, srcLen - 1);
                clip->samples[i] = (1.0f - frac) * collected[i0] + frac * collected[i1];
            }
        }
    }

    return clip;
}

} // namespace guitar_dsp::audio

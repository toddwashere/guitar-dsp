#include "PiperTTSSource.h"
#include "util/PluginLogger.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace guitar_dsp::audio {

namespace {

// Convert mono Int16 PCM to float and linear-resample to the target rate.
std::vector<float> int16PcmToFloatMono(const std::byte* bytes,
                                       std::size_t numBytes,
                                       double srcRate,
                                       double targetRate) {
    const std::size_t numSamples = numBytes / sizeof(std::int16_t);
    if (numSamples == 0) return {};
    const auto* src = reinterpret_cast<const std::int16_t*>(bytes);

    std::vector<float> floatSrc(numSamples);
    constexpr float invInt16 = 1.0f / 32768.0f;
    for (std::size_t i = 0; i < numSamples; ++i) {
        floatSrc[i] = static_cast<float>(src[i]) * invInt16;
    }

    if (std::abs(srcRate - targetRate) < 0.5) {
        return floatSrc;
    }
    const double ratio = srcRate / targetRate;
    const std::size_t outLen = static_cast<std::size_t>(static_cast<double>(numSamples) / ratio);
    if (outLen == 0) return {};
    std::vector<float> out(outLen);
    for (std::size_t i = 0; i < outLen; ++i) {
        const double srcIdx = static_cast<double>(i) * ratio;
        const std::size_t i0 = static_cast<std::size_t>(srcIdx);
        const float frac = static_cast<float>(srcIdx - static_cast<double>(i0));
        const std::size_t i1 = std::min(i0 + 1, numSamples - 1);
        out[i] = (1.0f - frac) * floatSrc[i0] + frac * floatSrc[i1];
    }
    return out;
}

// Read the voice's .onnx.json sidecar for the source sample rate. Falls
// back to 22050 (Piper's most common default) if the file is missing or
// malformed.
double readVoiceSampleRate(const std::string& voicePath) {
    const juce::File jf(voicePath + ".json");
    if (!jf.existsAsFile()) return 22050.0;
    const auto text = jf.loadFileAsString();
    auto parsed = juce::JSON::parse(text);
    if (auto* obj = parsed.getDynamicObject()) {
        if (obj->hasProperty("audio")) {
            if (auto* audio = obj->getProperty("audio").getDynamicObject()) {
                if (audio->hasProperty("sample_rate")) {
                    return static_cast<double>(audio->getProperty("sample_rate"));
                }
            }
        }
        if (obj->hasProperty("sample_rate")) {
            return static_cast<double>(obj->getProperty("sample_rate"));
        }
    }
    std::cerr << "[PiperTTSSource] voice JSON " << jf.getFullPathName()
              << " present but no sample_rate field found; defaulting to 22050 Hz\n";
    return 22050.0;
}

// Spawn the Piper binary, write `text` to its stdin, collect stdout.
// Returns the raw byte buffer (empty on failure). Times out after
// timeoutMs (SIGKILL).
//
// JUCE's juce::ChildProcess does not expose a stdin pipe API in 8.x — it
// only attaches a stdout pipe. We use POSIX directly for the stdin half.
// This file is macOS-only (the project already ships Objective-C++ for
// AppleTTSSource), so no portability concern.
std::vector<std::byte> runPiperSubprocess(const std::string& binaryPath,
                                          const std::string& voicePath,
                                          const std::string& text,
                                          int timeoutMs) {
    int stdinPipe[2]  = {-1, -1};
    int stdoutPipe[2] = {-1, -1};
    if (pipe(stdinPipe) != 0) return {};
    if (pipe(stdoutPipe) != 0) {
        ::close(stdinPipe[0]);
        ::close(stdinPipe[1]);
        return {};
    }

    const pid_t pid = fork();
    if (pid < 0) {
        ::close(stdinPipe[0]);  ::close(stdinPipe[1]);
        ::close(stdoutPipe[0]); ::close(stdoutPipe[1]);
        return {};
    }

    if (pid == 0) {
        // Child.
        ::dup2(stdinPipe[0],  STDIN_FILENO);
        ::dup2(stdoutPipe[1], STDOUT_FILENO);
        // Send stderr to /dev/null so Piper's progress chatter doesn't
        // clutter the host terminal during demos.
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) { ::dup2(devnull, STDERR_FILENO); ::close(devnull); }
        ::close(stdinPipe[0]);  ::close(stdinPipe[1]);
        ::close(stdoutPipe[0]); ::close(stdoutPipe[1]);
        ::execl(binaryPath.c_str(),
                binaryPath.c_str(),
                "--model", voicePath.c_str(),
                "--output-raw",
                static_cast<char*>(nullptr));
        ::_exit(127);  // execl returned — failed
    }

    // Parent.
    ::close(stdinPipe[0]);
    ::close(stdoutPipe[1]);

    // Write text + newline to child stdin, then close. EINTR-safe loop;
    // a real write failure (EPIPE etc.) means the child died early — log
    // so we don't silently truncate the prompt.
    const std::string toWrite = text + "\n";
    const char* p = toWrite.data();
    std::size_t remaining = toWrite.size();
    while (remaining > 0) {
        const ssize_t n = ::write(stdinPipe[1], p, remaining);
        if (n > 0) {
            p += n;
            remaining -= static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        std::cerr << "[PiperTTSSource] write to child stdin failed (errno="
                  << errno << ", " << remaining << " bytes unwritten)\n";
        break;
    }
    ::close(stdinPipe[1]);

    // Non-blocking read of stdout, with deadline.
    const int flags = ::fcntl(stdoutPipe[0], F_GETFL, 0);
    if (flags >= 0) ::fcntl(stdoutPipe[0], F_SETFL, flags | O_NONBLOCK);

    std::vector<std::byte> out;
    constexpr std::size_t bufSize = 8192;
    std::byte buf[bufSize];
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    bool childReaped = false;
    while (true) {
        const ssize_t n = ::read(stdoutPipe[0], buf, bufSize);
        if (n > 0) {
            out.insert(out.end(), buf, buf + n);
            continue;
        }
        if (n == 0) break;  // EOF

        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << "[PiperTTSSource] read error (errno=" << errno << ")\n";
            break;
        }

        // EAGAIN: check child status, then sleep briefly.
        int status = 0;
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            childReaped = true;
            // Drain anything remaining. Loop until a real EOF or hard
            // error — handle EAGAIN by spinning briefly because the
            // kernel may flush the pipe buffer just after the child
            // exits.
            const auto drainDeadline = juce::Time::getMillisecondCounterHiRes() + 250.0;
            while (true) {
                const ssize_t m = ::read(stdoutPipe[0], buf, bufSize);
                if (m > 0) { out.insert(out.end(), buf, buf + m); continue; }
                if (m == 0) break;  // EOF
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    if (juce::Time::getMillisecondCounterHiRes() > drainDeadline) break;
                    const struct timespec ts {0, 1'000'000};  // 1 ms
                    ::nanosleep(&ts, nullptr);
                    continue;
                }
                std::cerr << "[PiperTTSSource] drain read error (errno=" << errno << ")\n";
                break;
            }
            break;
        }

        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - t0;
        if (elapsed > timeoutMs) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            childReaped = true;
            ::close(stdoutPipe[0]);
            return {};
        }
        const struct timespec ts {0, 2'000'000};  // 2 ms
        ::nanosleep(&ts, nullptr);
    }
    if (!childReaped) {
        int status = 0;
        ::waitpid(pid, &status, 0);
    }
    ::close(stdoutPipe[0]);
    return out;
}

} // namespace

PiperTTSSource::PiperTTSSource(std::string binaryPath, std::string voicePath)
    : binaryPath_(std::move(binaryPath)), voicePath_(std::move(voicePath)) {}

void PiperTTSSource::prepare(double targetSampleRate) {
    targetSampleRate_ = targetSampleRate;
}

namespace {
// The piper binary loads these via @rpath = its own directory. The upstream
// macOS tarballs (2023.11.14-2) ship without them, so a present + executable
// binary still crashes at launch with "Library not loaded: ...".
constexpr const char* kRequiredPiperDylibs[] = {
    "libespeak-ng.1.dylib",
    "libpiper_phonemize.1.dylib",
    "libonnxruntime.1.14.1.dylib",
};
}

bool PiperTTSSource::isReady() const {
    return statusDetail().empty();
}

std::string PiperTTSSource::statusDetail() const {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (!fs::exists(binaryPath_, ec) || ec)
        return "piper binary missing at " + binaryPath_;
    if (!fs::exists(voicePath_, ec) || ec)
        return "voice file missing at " + voicePath_;

    const auto perms = fs::status(binaryPath_, ec).permissions();
    if (ec)
        return "could not stat piper binary at " + binaryPath_;
    if ((perms & (fs::perms::owner_exec | fs::perms::group_exec |
                  fs::perms::others_exec)) == fs::perms::none)
        return "piper binary at " + binaryPath_ + " is not executable";

    const fs::path binDir = fs::path(binaryPath_).parent_path();
    for (const auto* lib : kRequiredPiperDylibs) {
        const auto p = binDir / lib;
        if (!fs::exists(p, ec) || ec) {
            return std::string(lib) + " missing next to piper binary at "
                + binDir.string() + " — the upstream macOS tarball ships "
                "without runtime dylibs; build Piper from source or copy "
                "the .dylib from a working install";
        }
    }
    return {};
}

TTSClipPtr PiperTTSSource::synthesize(const std::string& text) {
    if (text.empty()) return nullptr;
    if (!isReady()) {
        guitar_dsp::log::warn("PiperTTS not ready: " + juce::String(statusDetail()));
        return nullptr;
    }

    const auto stdoutBytes = runPiperSubprocess(binaryPath_, voicePath_, text, 10000);
    if (stdoutBytes.empty()) {
        guitar_dsp::log::warn("PiperTTS subprocess produced no audio for: "
                              + juce::String(text.substr(0, 64)));
        return nullptr;
    }

    const double srcRate = readVoiceSampleRate(voicePath_);
    auto samples = int16PcmToFloatMono(stdoutBytes.data(),
                                        stdoutBytes.size(),
                                        srcRate,
                                        targetSampleRate_);

    auto clip = std::make_shared<TTSClip>();
    clip->name = text.substr(0, 32);
    clip->sampleRate = targetSampleRate_;
    clip->samples = std::move(samples);
    return clip;
}

} // namespace guitar_dsp::audio

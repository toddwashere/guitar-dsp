#include "PhonemeExtractor.h"

#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace guitar_dsp::audio {

namespace {

// Runs espeak-ng with `-q --pho` to get one phoneme per line w/ duration.
std::vector<std::pair<std::string,double>> runEspeak(
        const std::string& bin, const std::string& voice,
        const std::string& text) {
    int stdoutPipe[2] = {-1, -1};
    if (pipe(stdoutPipe) != 0) return {};
    const pid_t pid = fork();
    if (pid < 0) { ::close(stdoutPipe[0]); ::close(stdoutPipe[1]); return {}; }

    if (pid == 0) {
        // Child.
        ::dup2(stdoutPipe[1], STDOUT_FILENO);
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) { ::dup2(devnull, STDERR_FILENO); ::close(devnull); }
        ::close(stdoutPipe[0]); ::close(stdoutPipe[1]);
        ::execl(bin.c_str(), bin.c_str(),
                "-q", "--pho",
                "-v", voice.c_str(),
                text.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);
    }

    ::close(stdoutPipe[1]);
    std::string acc;
    char buf[4096];
    while (true) {
        const ssize_t n = ::read(stdoutPipe[0], buf, sizeof(buf));
        if (n > 0) { acc.append(buf, buf + n); continue; }
        if (n == 0) break;
        if (errno == EINTR) continue;
        break;
    }
    ::close(stdoutPipe[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);

    // Parse `--pho` format: one line per phoneme:
    //   "<label> <duration_ms>"
    std::vector<std::pair<std::string,double>> out;
    std::istringstream iss(acc);
    std::string line;
    while (std::getline(iss, line)) {
        std::istringstream ls(line);
        std::string label;
        double durMs = 0.0;
        if (ls >> label >> durMs && durMs > 0.0) {
            out.emplace_back(label, durMs);
        }
    }
    return out;
}

} // namespace

PhonemeExtractor::PhonemeExtractor(std::string binaryPath, std::string voice)
    : binaryPath_(std::move(binaryPath)), voice_(std::move(voice)) {}

bool PhonemeExtractor::isReady() const { return statusDetail().empty(); }

std::string PhonemeExtractor::statusDetail() const {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(binaryPath_, ec) || ec)
        return "espeak-ng binary missing at " + binaryPath_;
    const auto perms = fs::status(binaryPath_, ec).permissions();
    if (ec) return "could not stat espeak-ng at " + binaryPath_;
    if ((perms & (fs::perms::owner_exec | fs::perms::group_exec |
                  fs::perms::others_exec)) == fs::perms::none)
        return "espeak-ng at " + binaryPath_ + " is not executable";
    return {};
}

std::vector<Phoneme> PhonemeExtractor::extract(
        const std::string& text, double targetSampleRate) const {
    if (text.empty() || !isReady()) return {};
    const auto raw = runEspeak(binaryPath_, voice_, text);
    if (raw.empty()) {
        std::cerr << "[PhonemeExtractor] no phonemes for: " << text << '\n';
        return {};
    }
    std::vector<Phoneme> result;
    result.reserve(raw.size());
    std::size_t cursor = 0;
    for (const auto& [label, durMs] : raw) {
        const auto durSamples = static_cast<std::size_t>(
            durMs * 0.001 * targetSampleRate);
        Phoneme p;
        p.label = label;
        p.type  = phonemeType(label);
        p.startSample = cursor;
        p.endSample   = cursor + durSamples;
        result.push_back(p);
        cursor += durSamples;
    }
    return result;
}

} // namespace guitar_dsp::audio

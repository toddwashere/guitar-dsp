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

// Uniform duration assigned to each phoneme, in milliseconds.
// espeak-ng's `-x --sep=" "` output gives only labels, no timing.
// Callers (PhonemeAlignedClipBuilder) rescale these to match actual
// Piper audio length anyway, so the absolute value here just needs to be
// non-zero and consistent.
static constexpr double kDefaultPhonemeMs = 80.0;

// Runs `espeak-ng -q -x --sep=" "` to get space-separated phoneme labels.
// Returns one pair per phoneme: (label, kDefaultPhonemeMs).
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
        // Derive espeak-ng-data dir from the binary path (it lives in the
        // same directory as the binary, e.g. assets/piper/espeak-ng-data).
        const std::string dataDir =
            std::filesystem::path(bin).parent_path().string();
        const std::string pathArg = "--path=" + dataDir;
        // Use `-x --sep=" "` which writes space-separated phoneme mnemonics
        // to stdout and works with all standard espeak voices (no MBROLA needed).
        ::execl(bin.c_str(), bin.c_str(),
                pathArg.c_str(),
                "-q", "-x", "--sep= ",
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

    // Parse `-x --sep=" "` output: space-separated phoneme mnemonics on one
    // line, e.g. "h @ l 'oU  w '3: l d".
    // Strip leading stress markers (') and emit one entry per token.
    std::vector<std::pair<std::string,double>> out;
    std::istringstream iss(acc);
    std::string token;
    while (iss >> token) {
        // Remove leading stress marker if present.
        if (!token.empty() && token.front() == '\'')
            token.erase(token.begin());
        if (!token.empty())
            out.emplace_back(token, kDefaultPhonemeMs);
    }
    return out;
}

} // anonymous namespace

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

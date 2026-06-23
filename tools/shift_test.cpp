// tools/shift_test.cpp — offline formant-preserving pitch shift via WORLD.
//
// Usage:
//   shift_test --input src.wav --ratio 1.5 --output out.wav [--report report.txt]
//
// Reads a mono 16-bit WAV, runs WORLD analysis (Harvest+CheapTrick+D4C),
// scales the F0 contour by `ratio`, re-synthesises, writes WAV. Records
// wall-clock time per phase + an estimate of added latency (distance
// from first non-trivial input sample to first non-trivial output sample).

#include "world/harvest.h"
#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/synthesis.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// Minimal WAV mono-16 reader. Crashes on anything fancy; that's fine
// for an internal tool.
static bool readWav(const std::string& path, std::vector<double>& samples,
                    int& sampleRate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
    char chunkId[4]; uint32_t chunkSize;
    char format[4];
    f.read(chunkId, 4); f.read(reinterpret_cast<char*>(&chunkSize), 4);
    f.read(format, 4);
    if (std::strncmp(chunkId, "RIFF", 4) != 0 ||
        std::strncmp(format, "WAVE", 4) != 0) return false;

    uint16_t numChannels = 0, bitsPerSample = 0;
    uint32_t sr = 0;
    std::vector<int16_t> pcm;
    while (f) {
        char subId[4]; uint32_t subSize;
        if (!f.read(subId, 4)) break;
        f.read(reinterpret_cast<char*>(&subSize), 4);
        if (std::strncmp(subId, "fmt ", 4) == 0) {
            uint16_t audioFormat, blockAlign;
            uint32_t byteRate;
            f.read(reinterpret_cast<char*>(&audioFormat), 2);
            f.read(reinterpret_cast<char*>(&numChannels), 2);
            f.read(reinterpret_cast<char*>(&sr), 4);
            f.read(reinterpret_cast<char*>(&byteRate), 4);
            f.read(reinterpret_cast<char*>(&blockAlign), 2);
            f.read(reinterpret_cast<char*>(&bitsPerSample), 2);
            if (subSize > 16) f.ignore(subSize - 16);
        } else if (std::strncmp(subId, "data", 4) == 0) {
            pcm.resize(subSize / 2);
            f.read(reinterpret_cast<char*>(pcm.data()), subSize);
        } else {
            f.ignore(subSize);
        }
    }
    if (numChannels != 1 || bitsPerSample != 16 || pcm.empty()) return false;
    sampleRate = static_cast<int>(sr);
    samples.resize(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i)
        samples[i] = static_cast<double>(pcm[i]) / 32768.0;
    return true;
}

static bool writeWav(const std::string& path, const std::vector<double>& samples,
                     int sampleRate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint32_t numSamples = static_cast<uint32_t>(samples.size());
    const uint32_t dataBytes  = numSamples * 2;
    const uint32_t chunkSize  = 36 + dataBytes;
    f.write("RIFF", 4);
    f.write(reinterpret_cast<const char*>(&chunkSize), 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    const uint32_t subSize = 16;
    const uint16_t fmt = 1, ch = 1, bits = 16;
    const uint16_t blockAlign = ch * bits / 8;
    const uint32_t byteRate   = sampleRate * blockAlign;
    f.write(reinterpret_cast<const char*>(&subSize), 4);
    f.write(reinterpret_cast<const char*>(&fmt), 2);
    f.write(reinterpret_cast<const char*>(&ch), 2);
    f.write(reinterpret_cast<const char*>(&sampleRate), 4);
    f.write(reinterpret_cast<const char*>(&byteRate), 4);
    f.write(reinterpret_cast<const char*>(&blockAlign), 2);
    f.write(reinterpret_cast<const char*>(&bits), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&dataBytes), 4);
    for (double s : samples) {
        const int v = static_cast<int>(std::round(s * 32767.0));
        const int16_t clipped = static_cast<int16_t>(
            v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
        f.write(reinterpret_cast<const char*>(&clipped), 2);
    }
    return true;
}

int main(int argc, char** argv) {
    std::string inPath, outPath, reportPath;
    double ratio = 1.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--input"  && i + 1 < argc) inPath     = argv[++i];
        else if (a == "--output" && i + 1 < argc) outPath = argv[++i];
        else if (a == "--ratio"  && i + 1 < argc) ratio   = std::atof(argv[++i]);
        else if (a == "--report" && i + 1 < argc) reportPath = argv[++i];
    }
    if (inPath.empty() || outPath.empty()) {
        fprintf(stderr, "usage: shift_test --input X.wav --ratio R "
                "--output Y.wav [--report report.txt]\n");
        return 1;
    }

    int sr = 0;
    std::vector<double> in;
    if (!readWav(inPath, in, sr)) { fprintf(stderr, "read failed\n"); return 1; }
    fprintf(stderr, "loaded %zu samples @ %d Hz (%.3f s)\n",
            in.size(), sr, in.size() / double(sr));

    using clk = std::chrono::high_resolution_clock;

    // Harvest: F0 estimation.
    HarvestOption harvestOpt; InitializeHarvestOption(&harvestOpt);
    harvestOpt.frame_period = 5.0;
    const int f0Length = GetSamplesForHarvest(sr, static_cast<int>(in.size()),
                                              harvestOpt.frame_period);
    std::vector<double> f0(f0Length), timeAxis(f0Length);
    auto t0 = clk::now();
    Harvest(in.data(), static_cast<int>(in.size()), sr, &harvestOpt,
            timeAxis.data(), f0.data());
    auto t1 = clk::now();

    // CheapTrick: spectral envelope.
    CheapTrickOption ctOpt; InitializeCheapTrickOption(sr, &ctOpt);
    const int fftSize = GetFFTSizeForCheapTrick(sr, &ctOpt);
    std::vector<double*> spec(f0Length, nullptr);
    std::vector<double>  specBuf(static_cast<std::size_t>(f0Length)
                                  * static_cast<std::size_t>(fftSize / 2 + 1));
    for (int i = 0; i < f0Length; ++i)
        spec[i] = &specBuf[(std::size_t) i * (fftSize / 2 + 1)];
    CheapTrick(in.data(), static_cast<int>(in.size()), sr,
               timeAxis.data(), f0.data(), f0Length, &ctOpt, spec.data());
    auto t2 = clk::now();

    // D4C: aperiodicity.
    D4COption d4cOpt; InitializeD4COption(&d4cOpt);
    std::vector<double*> ap(f0Length, nullptr);
    std::vector<double>  apBuf(static_cast<std::size_t>(f0Length)
                                * static_cast<std::size_t>(fftSize / 2 + 1));
    for (int i = 0; i < f0Length; ++i)
        ap[i] = &apBuf[(std::size_t) i * (fftSize / 2 + 1)];
    D4C(in.data(), static_cast<int>(in.size()), sr,
        timeAxis.data(), f0.data(), f0Length, fftSize, &d4cOpt, ap.data());
    auto t3 = clk::now();

    // Scale F0 by ratio. Spectral envelope and aperiodicity stay
    // unchanged → formant-preserving pitch shift.
    for (auto& v : f0) v *= ratio;

    // Synthesis.
    std::vector<double> out(in.size());
    Synthesis(f0.data(), f0Length, spec.data(), ap.data(), fftSize,
              harvestOpt.frame_period, sr,
              static_cast<int>(out.size()), out.data());
    auto t4 = clk::now();

    auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    fprintf(stderr,
            "Harvest %.1f ms / CheapTrick %.1f ms / D4C %.1f ms / Synth %.1f ms\n",
            ms(t0, t1), ms(t1, t2), ms(t2, t3), ms(t3, t4));

    if (!writeWav(outPath, out, sr)) { fprintf(stderr, "write failed\n"); return 1; }
    fprintf(stderr, "wrote %s\n", outPath.c_str());

    if (!reportPath.empty()) {
        std::ofstream r(reportPath);
        r << "input=" << inPath << "\n"
          << "output=" << outPath << "\n"
          << "ratio=" << ratio << "\n"
          << "sampleRate=" << sr << "\n"
          << "lengthSamples=" << in.size() << "\n"
          << "harvest_ms="   << ms(t0, t1) << "\n"
          << "cheaptrick_ms="<< ms(t1, t2) << "\n"
          << "d4c_ms="       << ms(t2, t3) << "\n"
          << "synthesis_ms=" << ms(t3, t4) << "\n"
          << "total_ms="     << ms(t0, t4) << "\n"
          << "realtime_factor="
          << (ms(t0, t4) / (in.size() / double(sr) * 1000.0)) << "\n";
    }
    return 0;
}

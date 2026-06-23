#include "GrainAnalyser.h"

#include "world/harvest.h"
#include "world/cheaptrick.h"
#include "world/d4c.h"

#include <vector>

namespace guitar_dsp::audio {

std::shared_ptr<ShifterGrain>
analyseGrain(const float* samples, int numSamples, int sampleRate) {
    if (! samples || numSamples <= 0 || sampleRate <= 0) return nullptr;

    // Convert float32 input to double for WORLD.
    std::vector<double> x(static_cast<std::size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        x[static_cast<std::size_t>(i)] = static_cast<double>(samples[i]);

    auto g = std::make_shared<ShifterGrain>();
    g->sampleRate    = sampleRate;
    g->framePeriodMs = 5.0;

    // --- F0 estimation (Harvest) ---
    HarvestOption hopt;
    InitializeHarvestOption(&hopt);
    hopt.frame_period = g->framePeriodMs;

    const int f0Length = GetSamplesForHarvest(sampleRate, numSamples,
                                              hopt.frame_period);
    g->f0.assign(static_cast<std::size_t>(f0Length), 0.0);
    g->timeAxis.assign(static_cast<std::size_t>(f0Length), 0.0);
    Harvest(x.data(), numSamples, sampleRate, &hopt,
            g->timeAxis.data(), g->f0.data());

    // --- Spectral envelope (CheapTrick) ---
    CheapTrickOption ctOpt;
    InitializeCheapTrickOption(sampleRate, &ctOpt);
    g->fftSize = GetFFTSizeForCheapTrick(sampleRate, &ctOpt);
    const int bins = g->fftSize / 2 + 1;

    g->spectrum.assign(static_cast<std::size_t>(f0Length),
                       std::vector<double>(static_cast<std::size_t>(bins), 0.0));
    std::vector<double*> specPtr(static_cast<std::size_t>(f0Length));
    for (int i = 0; i < f0Length; ++i)
        specPtr[static_cast<std::size_t>(i)] = g->spectrum[static_cast<std::size_t>(i)].data();
    CheapTrick(x.data(), numSamples, sampleRate,
               g->timeAxis.data(), g->f0.data(), f0Length,
               &ctOpt, specPtr.data());

    // --- Aperiodicity (D4C) ---
    D4COption dOpt;
    InitializeD4COption(&dOpt);

    g->aperiodicity.assign(static_cast<std::size_t>(f0Length),
                           std::vector<double>(static_cast<std::size_t>(bins), 0.0));
    std::vector<double*> apPtr(static_cast<std::size_t>(f0Length));
    for (int i = 0; i < f0Length; ++i)
        apPtr[static_cast<std::size_t>(i)] = g->aperiodicity[static_cast<std::size_t>(i)].data();
    D4C(x.data(), numSamples, sampleRate,
        g->timeAxis.data(), g->f0.data(), f0Length, g->fftSize, &dOpt,
        apPtr.data());

    return g;
}

} // namespace guitar_dsp::audio

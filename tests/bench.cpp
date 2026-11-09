// Real-time budget of the core, on the module's path: the regeneration of four busy tracks when a
// seed moves (setSettings, on the engine thread in this line), the load of a bank (setPatterns),
// and the render cost per sample with the clock follower and the CV voices, in per-sample blocks
// as Rack runs them, and in blocks of 32. Build and run: make bench
#include "cv.h"
#include "params.h"
#include "sequencer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

using namespace tetrad;

namespace {

double since(std::chrono::steady_clock::time_point a)
{
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count();
}

} // namespace

int main()
{
    constexpr double kRate = 48000.0;
    constexpr double kBudget = 1e6 / kRate;   // µs per sample at 48 kHz
    double v[kNumParams];
    defaultParamValues(v);
    auto tp = [&](int t, int p, double x) { v[trackParamIndex(t, p)] = x; };
    for (int t = 0; t < kNumTracks; ++t) {
        tp(t, kTrackEnabled, 1);
        tp(t, kTrackDensity, 90);
        tp(t, kTrackRatchet, 40);
        tp(t, kTrackVariation, 30);
    }
    tp(1, kTrackDivision, 2);
    tp(2, kTrackMode, kModePoly);
    tp(2, kTrackVoices, 7);
    tp(3, kTrackMode, kModeDrum);
    Sequencer seq;
    seq.reset(kRate);

    // Regeneration: 200 seed moves of the four tracks.
    double worstGen = 0, totalGen = 0;
    for (int i = 0; i < 200; ++i) {
        for (int t = 0; t < kNumTracks; ++t)
            tp(t, kTrackSeed, 1 + (i * 7 + t * 13) % 999);
        const auto a = std::chrono::steady_clock::now();
        seq.setSettings(settingsFromValues(v));
        const double us = since(a);
        worstGen = std::max(worstGen, us);
        totalGen += us;
    }
    std::printf("regeneration of 4 tracks (setSettings): mean %.1f us, worst %.1f us\n", totalGen / 200, worstGen);

    const PatternBank bank = seq.patterns();
    double worstLoad = 0;
    for (int i = 0; i < 200; ++i) {
        const auto a = std::chrono::steady_clock::now();
        seq.setPatterns(bank);
        worstLoad = std::max(worstLoad, since(a));
    }
    std::printf("load of a bank (setPatterns): worst %.1f us\n", worstLoad);

    // Render: 60 s at 48 kHz, external clock at 120 BPM (4 PPQN), CV voices as the module.
    for (int block : {1, 32}) {
        Sequencer s;
        s.reset(kRate);
        s.setSettings(settingsFromValues(v));
        ClockFollower clock;
        clock.setPpqn(4);
        CvVoices voices;
        for (int t = 0; t < kNumTracks; ++t)
            voices.setMode(t, t == 2 ? kModePoly : t == 3 ? kModeDrum : kModeMono);
        const long n = long(60 * kRate);
        double worst = 0;
        const auto start = std::chrono::steady_clock::now();
        for (long i = 0; i < n; i += block) {
            const auto a = std::chrono::steady_clock::now();
            TransportState tr{};
            for (int k = 0; k < block; ++k) {
                voices.beginSample();
                tr = clock.tick((i + k) % 6000 == 0, true, kRate);
            }
            s.render(tr, 0, uint32_t(block), voices);
            worst = std::max(worst, since(a) / block);
        }
        const double perSample = since(start) / double(n);
        std::printf("render, blocks of %2d: %.3f us per sample (%.2f %% of the 48 kHz budget), worst block %.2f us per sample\n",
                    block, perSample, 100.0 * perSample / kBudget, worst);
    }
    return 0;
}

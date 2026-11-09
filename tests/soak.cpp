// Endurance: one hour of playback at 48 kHz in per-sample blocks (as Rack), four busy tracks, an
// external clock that stops now and then (the 2.7.1 restart), RUN starts, the generator settings
// moving every two seconds and RESET now and then, through the clock follower and the CV voices as
// the module runs them. Checks that every note is paired and ends, and reports the worst time of
// one sample: its centiles (the maximum of a process that is not real-time is the operating
// system's, not Tetrad's), with and without a regeneration in it. Build and run: make soak
#include "cv.h"
#include "params.h"
#include "rng.h"
#include "sequencer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace tetrad;

namespace {

struct Pairs final : NoteSink {
    NoteSink* next = nullptr;
    bool sounding[kNumTracks][128] = {};
    long notes = 0, errors = 0;
    void emit(const NoteEvent& e) override
    {
        bool& on = sounding[e.track][e.key];
        if (e.on == on)
            ++errors;
        on = e.on;
        notes += e.on;
        next->emit(e);
    }
    int open() const
    {
        int n = 0;
        for (const auto& t : sounding)
            for (bool b : t)
                n += b;
        return n;
    }
};

} // namespace

int main()
{
    constexpr double kRate = 48000.0;
    constexpr long kSamples = long(3600 * kRate);
    double v[kNumParams];
    defaultParamValues(v);
    auto tp = [&](int t, int p, double x) { v[trackParamIndex(t, p)] = x; };
    for (int t = 0; t < kNumTracks; ++t) {
        tp(t, kTrackEnabled, 1);
        tp(t, kTrackDensity, 80);
        tp(t, kTrackRatchet, 30);
        tp(t, kTrackVariation, 30);
    }
    tp(2, kTrackMode, kModePoly);
    tp(2, kTrackVoices, 5);
    tp(3, kTrackMode, kModeDrum);
    Rng rng(2026);
    Sequencer seq;
    seq.reset(kRate);
    seq.setSettings(settingsFromValues(v));
    ClockFollower clock;
    clock.setPpqn(4);
    CvVoices voices;
    for (int t = 0; t < kNumTracks; ++t)
        voices.setMode(t, t == 2 ? kModePoly : t == 3 ? kModeDrum : kModeMono);
    Pairs pairs;
    pairs.next = &voices;
    long nextPulse = 0, period = 6000, stopUntil = -1;
    std::vector<long> histogram(1001, 0);   // 0.1 us buckets up to 100 us, then beyond
    double worst = 0, worstGen = 0;
    long regenerations = 0, stops = 0, rewinds = 0;
    const auto start = std::chrono::steady_clock::now();
    for (long i = 0; i < kSamples; ++i) {
        const auto a = std::chrono::steady_clock::now();
        bool regen = false;
        if (i % long(2 * kRate) == 0 && i > 0) {   // a setting moves every two seconds
            const int t = int(rng.below(kNumTracks));
            tp(t, kTrackSeed, 1 + rng.below(999));
            tp(t, kTrackDensity, 20 + rng.below(81));
            seq.setSettings(settingsFromValues(v));
            regen = true;
            ++regenerations;
        }
        if (rng.below(long(30 * kRate)) == 0) {    // RESET now and then
            seq.rewind();
            ++rewinds;
        }
        bool pulse = false;
        if (i >= stopUntil && i >= nextPulse) {
            pulse = true;
            if (rng.below(400) == 0) {             // the clock stops for 1 to 4 s
                stopUntil = i + long(kRate) * (1 + long(rng.below(4)));
                nextPulse = stopUntil;
                ++stops;
                if (rng.below(2) == 0)
                    clock.skipNextGap();           // started by a RUN trigger
            } else {
                if (rng.below(200) == 0)
                    period = 3000 + long(rng.below(9000));   // a new tempo
                nextPulse = i + period;
            }
        }
        voices.beginSample();
        const TransportState tr = clock.tick(pulse, true, kRate);
        seq.render(tr, 0, 1, pairs);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count();
        (regen ? worstGen : worst) = std::max(regen ? worstGen : worst, us);
        if (!regen)
            ++histogram[size_t(std::min(us * 10.0, 1000.0))];
    }
    seq.allNotesOff(0, pairs);
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("soak: 1 h at 48 kHz in %.0f s; %ld notes, %ld regenerations, %ld clock stops, %ld resets\n",
                total, pairs.notes, regenerations, stops, rewinds);
    auto centile = [&](double q) {
        long seen = 0;
        const long goal = long(q * double(kSamples - regenerations));
        for (size_t b = 0; b < histogram.size(); ++b)
            if ((seen += histogram[b]) >= goal)
                return double(b) / 10.0;
        return 100.0;
    };
    std::printf("one sample: median %.1f us, 99.99 %% %.1f us, 99.9999 %% %.1f us, worst %.0f us (the system's)\n",
                centile(0.5), centile(0.9999), centile(0.999999), worst);
    std::printf("a sample with a regeneration: worst %.1f us (Rack computes blocks of samples: one such sample\n"
                "  every two seconds is absorbed by its block; the budget is 20.8 us per sample on average)\n", worstGen);
    std::printf("pairing errors: %ld, notes left open: %d\n", pairs.errors, pairs.open());
    const bool ok = pairs.errors == 0 && pairs.open() == 0 && pairs.notes > 50000;   // not an empty proof
    std::printf(ok ? "soak: OK\n" : "soak: FAIL\n");
    return ok ? 0 : 1;
}

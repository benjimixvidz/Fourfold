#include "pattern.h"

#include "rng.h"

#include <algorithm>

namespace tetrad {

void generatePattern(const GenKey& key, TrackPattern& out)
{
    Rng rng(mix64(uint64_t(uint32_t(key.seed)) * 0x100000001B3ull + 0x5EEDull));
    const int range = std::clamp(key.range, 1, 24);
    const int density = std::clamp(key.density, 0, 100);
    const int voices = std::clamp(key.voices, 1, kMaxVoices);
    const int ratchet = std::clamp(key.ratchet, 0, 100);
    // Separate stream: changing the ratchet amount leaves the notes untouched.
    Rng ratchetRng(mix64(uint64_t(uint32_t(key.seed)) ^ 0xA7C3E1ull));
    const int variation = std::clamp(key.variation, 0, 100);
    Rng conditionRng(mix64(uint64_t(uint32_t(key.seed)) ^ 0xC0D17Eull));

    int prev = 0;
    for (int i = 0; i < kMaxSteps; ++i) {
        Step s;
        // Downbeats are a little more likely to sound.
        const int bias = (i % 4 == 0) ? 15 : 0;
        s.active = rng.percent() < std::min(100, density + (density > 0 ? bias : 0));

        // Mostly stepwise motion with occasional leaps, kept inside [0, range).
        int degree;
        const int r = rng.percent();
        if (r < 25)
            degree = prev;
        else if (r < 60)
            degree = prev + (rng.below(2) ? 1 : -1);
        else
            degree = int(rng.below(uint32_t(range)));
        degree = std::clamp(degree, 0, range - 1);
        prev = degree;

        // Extra voices stack thirds inside the scale (degree + 2, + 4, + 6, ...).
        for (int v = 0; v < kMaxVoices; ++v)
            s.degree[v] = int8_t(degree + 2 * v);
        s.numNotes = uint8_t(voices);

        s.velocity = uint8_t(i % 4 == 0 ? 100 + rng.below(28) : 70 + rng.below(40));
        s.gate = 100;
        s.probability = 100;
        const bool ratchetHit = ratchetRng.percent() < ratchet;
        const uint8_t ratchetCount = uint8_t(2 + ratchetRng.below(3));   // 2..4 hits
        s.ratchet = (s.active && ratchetHit) ? ratchetCount : 1;

        // Variation: some steps only play on some loops (mostly A:B over 2 or 4 loops).
        const bool varied = conditionRng.percent() < variation;
        const int pick = conditionRng.percent();
        const int b = conditionRng.below(2) ? 4 : 2;
        const int a = 1 + int(conditionRng.below(uint32_t(b)));
        if (s.active && varied) {
            if (pick < 60) {
                s.condition = kCondAB;
                s.condA = uint8_t(a);
                s.condB = uint8_t(b);
            } else if (pick < 75) {
                s.condition = kCondNotAB;
                s.condA = uint8_t(a);
                s.condB = uint8_t(b);
            } else if (pick < 85) {
                s.condition = kCondPre;
            } else if (pick < 95) {
                s.condition = kCondNotPre;
            } else {
                s.condition = kCondNotFirst;
            }
        }
        out.steps[i] = s;
    }
    out.key = key;
}

} // namespace tetrad

#include "pattern.h"

#include "rng.h"

#include <algorithm>

namespace tetrad {

namespace {

// Drum hits, from their own random streams (the notes stay what they were). Each row has a role,
// as a drum machine's first eight voices: row 1 kick on the downbeats, row 2 snare on the
// backbeat, row 3 closed hat on eighths, row 4 open hat on the off-beats, rows 5-8 sparse
// percussion. These chances hold at DENS 75 % (the default) and scale with it; RTCH and VARI
// add ratchets and conditions to some hits.
void generateDrums(const GenKey& key, TrackPattern& out)
{
    const uint64_t seed = uint64_t(uint32_t(key.seed));
    Rng rng(mix64(seed * 0x9E3779B97F4A7C15ull + 0xD12Cull));
    Rng ratchetRng(mix64(seed ^ 0xD12C7A7Eull));
    Rng conditionRng(mix64(seed ^ 0xD12CC0DEull));
    const int density = std::clamp(key.density, 0, 100);
    const int ratchet = std::clamp(key.ratchet, 0, 100);
    const int variation = std::clamp(key.variation, 0, 100);
    for (int i = 0; i < kMaxSteps; ++i) {
        Step& s = out.steps[i];
        s.hits = 0;
        const int inBar = i % 16;
        for (int row = 0; row < kDrumRows; ++row) {
            int chance;   // % at full density
            switch (row) {
            case 0: chance = inBar % 4 == 0 ? 95 : (inBar % 2 == 0 ? 15 : 5); break;
            case 1: chance = inBar == 4 || inBar == 12 ? 95 : 6; break;
            case 2: chance = inBar % 2 == 0 ? 85 : 25; break;
            case 3: chance = inBar % 4 == 2 ? 55 : 3; break;
            default: chance = 12; break;
            }
            DrumHit& h = s.drum[row];
            h = DrumHit{};
            const bool hit = density > 0 && int(rng.percent()) < std::min(100, chance * density / 75);
            h.velocity = uint8_t(inBar % 4 == 0 ? 100 + rng.below(28) : 60 + rng.below(50));
            const bool ratchetHit = int(ratchetRng.percent()) < ratchet;
            const uint8_t count = uint8_t(2 + ratchetRng.below(3));
            const bool varied = int(conditionRng.percent()) < variation;
            const int b = conditionRng.below(2) ? 4 : 2;
            const int a = 1 + int(conditionRng.below(uint32_t(b)));
            if (!hit)
                continue;
            s.hits |= uint8_t(1u << row);
            if (ratchetHit && row >= 2)   // rolls on the hats and percussion, not the kick or snare
                h.ratchet = count;
            if (varied) {
                h.condition = kCondAB;
                h.condA = uint8_t(a);
                h.condB = uint8_t(b);
            }
        }
    }
}

} // namespace

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
    generateDrums(key, out);
    out.key = key;
}

} // namespace tetrad

#include "state.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace tetrad {

namespace {

constexpr const char* kMagic = "tetrad-seq-state";
constexpr int kVersion = 4;
constexpr int kStepFields = 11 + kMaxVoices;
constexpr int kHitFields = 7;
constexpr int kDrumFields = 3 + kDrumRows * kHitFields;

// Exactly `count` integers left on the line.
bool readInts(std::istringstream& in, long long* out, int count)
{
    std::string token;
    for (int i = 0; i < count; ++i) {
        if (!(in >> token))
            return false;
        char* end = nullptr;
        errno = 0;
        out[i] = std::strtoll(token.c_str(), &end, 10);
        if (errno || *end)
            return false;
    }
    return !(in >> token);
}

template <typename T>
T clampTo(long long v, long long lo, long long hi)
{
    return T(std::clamp(v, lo, hi));
}

bool sameHit(const DrumHit& a, const DrumHit& b)
{
    return a.velocity == b.velocity && a.gate == b.gate && a.probability == b.probability && a.ratchet == b.ratchet &&
           a.condition == b.condition && a.condA == b.condA && a.condB == b.condB;
}

bool defaultDrum(const Step& s)
{
    const DrumHit def;
    if (s.hits)
        return false;
    for (const DrumHit& h : s.drum)
        if (!sameHit(h, def))
            return false;
    return true;
}

} // namespace

std::string serializePatterns(const PatternBank& bank)
{
    std::string out;
    out.reserve(64 * 1024);
    char line[512];
    std::snprintf(line, sizeof(line), "%s %d\n", kMagic, kVersion);
    out += line;
    for (int t = 0; t < kNumTracks; ++t) {
        const TrackPattern& tp = bank.tracks[t];
        std::snprintf(line, sizeof(line), "gen %d %d %d %d %d %d %d\n", t, tp.key.seed, tp.key.density,
                      tp.key.range, tp.key.voices, tp.key.ratchet, tp.key.variation);
        out += line;
        for (int r = 0; r < kDrumRows; ++r) {
            const RowSettings& rs = tp.rows[r];
            std::snprintf(line, sizeof(line), "row %d %d %d %d %d\n", t, r, rs.level, rs.probability, rs.mute ? 1 : 0);
            out += line;
        }
        for (int i = 0; i < kMaxSteps; ++i) {
            const Step& s = tp.steps[i];
            std::snprintf(line, sizeof(line), "step %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", t, i,
                          s.active ? 1 : 0, s.velocity, s.gate, s.probability, s.ratchet, s.condition, s.condA,
                          s.condB, s.numNotes, s.degree[0], s.degree[1], s.degree[2], s.degree[3], s.degree[4],
                          s.degree[5], s.degree[6]);
            out += line;
            if (defaultDrum(s))
                continue;
            int n = std::snprintf(line, sizeof(line), "drum %d %d %d", t, i, s.hits);
            for (const DrumHit& h : s.drum)
                n += std::snprintf(line + n, sizeof(line) - size_t(n), " %d %d %d %d %d %d %d", h.velocity, h.gate,
                                   h.probability, h.ratchet, h.condition, h.condA, h.condB);
            out += line;
            out += '\n';
        }
    }
    out += "end\n";
    return out;
}

bool deserializePatterns(const std::string& text, PatternBank& bank)
{
    std::istringstream lines(text);
    std::string line, word;
    long long version = 0;
    if (!std::getline(lines, line))
        return false;
    {
        std::istringstream in(line);
        if (!(in >> word) || word != kMagic || !readInts(in, &version, 1) || version < 3 || version > kVersion)
            return false;
    }

    PatternBank out;   // default keys (seed -1) make the sequencer regenerate missing tracks
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        if (!(in >> word))
            continue;
        if (word == "end") {
            bank = out;
            return true;
        }
        if (word == "gen") {
            long long f[7];
            if (!readInts(in, f, 7) || f[0] < 0 || f[0] >= kNumTracks)
                return false;
            GenKey& key = out.tracks[f[0]].key;
            key.seed = clampTo<int>(f[1], -1, 9999);
            key.density = clampTo<int>(f[2], 0, 100);
            key.range = clampTo<int>(f[3], 1, 24);
            key.voices = clampTo<int>(f[4], 1, kMaxVoices);
            key.ratchet = clampTo<int>(f[5], 0, 100);
            key.variation = clampTo<int>(f[6], 0, 100);
        } else if (word == "step") {
            long long f[kStepFields];
            if (!readInts(in, f, kStepFields) || f[0] < 0 || f[0] >= kNumTracks || f[1] < 0 || f[1] >= kMaxSteps)
                return false;
            Step& s = out.tracks[f[0]].steps[f[1]];
            s.active = f[2] != 0;
            s.velocity = clampTo<uint8_t>(f[3], 1, 127);
            s.gate = f[4] > 100 ? kGateTie : clampTo<uint8_t>(f[4], 1, 100);
            s.probability = clampTo<uint8_t>(f[5], 0, 100);
            s.ratchet = clampTo<uint8_t>(f[6], 1, kMaxRatchet);
            s.condition = f[7] >= 0 && f[7] < kCondCount ? uint8_t(f[7]) : uint8_t(kCondAlways);
            s.condB = clampTo<uint8_t>(f[9], 1, 8);
            s.condA = clampTo<uint8_t>(f[8], 1, s.condB);
            s.numNotes = clampTo<uint8_t>(f[10], 1, kMaxVoices);
            for (int v = 0; v < kMaxVoices; ++v)
                s.degree[v] = clampTo<int8_t>(f[11 + v], -100, 100);
        } else if (word == "row") {
            long long f[5];
            if (!readInts(in, f, 5) || f[0] < 0 || f[0] >= kNumTracks || f[1] < 0 || f[1] >= kDrumRows)
                return false;
            RowSettings& rs = out.tracks[f[0]].rows[f[1]];
            rs.level = clampTo<uint8_t>(f[2], 0, 200);
            rs.probability = clampTo<uint8_t>(f[3], 0, 100);
            rs.mute = f[4] != 0;
        } else if (word == "drum") {
            long long f[kDrumFields];
            if (!readInts(in, f, kDrumFields) || f[0] < 0 || f[0] >= kNumTracks || f[1] < 0 || f[1] >= kMaxSteps)
                return false;
            Step& s = out.tracks[f[0]].steps[f[1]];
            s.hits = clampTo<uint8_t>(f[2], 0, 255);
            for (int r = 0; r < kDrumRows; ++r) {
                const long long* h = f + 3 + r * kHitFields;
                DrumHit& d = s.drum[r];
                d.velocity = clampTo<uint8_t>(h[0], 1, 127);
                d.gate = h[1] > 100 ? kGateTie : clampTo<uint8_t>(h[1], 1, 100);
                d.probability = clampTo<uint8_t>(h[2], 0, 100);
                d.ratchet = clampTo<uint8_t>(h[3], 1, kMaxRatchet);
                d.condition = h[4] >= 0 && h[4] < kCondCount ? uint8_t(h[4]) : uint8_t(kCondAlways);
                d.condB = clampTo<uint8_t>(h[6], 1, 8);
                d.condA = clampTo<uint8_t>(h[5], 1, d.condB);
            }
        }
    }
    return false;   // no "end": truncated
}

} // namespace tetrad

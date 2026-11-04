#include "params.h"

#include "scales.h"

#include <cmath>
#include <cstdio>

namespace tetrad {

namespace {

const char* const kOffOn[] = {"Off", "On", nullptr};
const char* const kKeys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", nullptr};
const char* const kModes[] = {"Mono", "Poly", nullptr};
const char* const kDirections[] = {"Forward", "Backward", "Ping-pong", "Random", "Drunk", nullptr};
// Indices are saved in patches: append only.
const char* const kDivisions[] = {"1/1",  "1/2",  "1/4",   "1/8",   "1/16",  "1/32",
                                  "1/4T", "1/8T", "1/16T", "1/32T", "1/64",  nullptr};
const double kDivisionBeats[] = {4.0,     2.0,     1.0,      0.5,      0.25,   0.125,
                                 2.0 / 3, 1.0 / 3, 1.0 / 6,  1.0 / 12, 1.0 / 16};
constexpr int kNumDivisions = int(sizeof(kDivisionBeats) / sizeof(kDivisionBeats[0]));
constexpr int kDefaultDivision = 4;

int countLabels(const char* const* labels)
{
    int n = 0;
    while (labels[n])
        ++n;
    return n;
}

// Track 1: 16th-note lead, 2: bass line, 3: slow poly chords, 4: sparse high line.
// Only track 1 starts enabled.
struct TrackDefaults {
    int enabled, mode, length, division, octave, gate, seed, density, range, voices;
};
const TrackDefaults kTrackDefaults[kNumTracks] = {
    {1, kModeMono, 16, 4, 4, 50, 1, 75, 8, 1},
    {0, kModeMono, 8, 3, 2, 80, 2, 60, 5, 1},
    {0, kModePoly, 4, 2, 4, 95, 3, 100, 7, 3},
    {0, kModeMono, 16, 4, 5, 25, 4, 50, 12, 1},
};

ParamDesc makeInt(const char* name, double mn, double mx, double def, const char* suffix = "", bool sign = false)
{
    ParamDesc d{};
    std::snprintf(d.name, sizeof(d.name), "%s", name);
    d.type = ParamType::Int;
    d.min = mn;
    d.max = mx;
    d.def = def;
    d.suffix = suffix;
    d.showSign = sign;
    return d;
}

ParamDesc makeEnum(const char* name, const char* const* labels, double def, ParamType type = ParamType::Enum)
{
    ParamDesc d = makeInt(name, 0, countLabels(labels) - 1, def);
    d.type = type;
    d.labels = labels;
    return d;
}

ParamDesc makeReserved()
{
    ParamDesc d = makeInt("Reserved", 0, 1, 0);
    d.type = ParamType::Reserved;
    return d;
}

struct Table {
    ParamDesc params[kNumParams];

    Table()
    {
        params[globalParamIndex(kGlobalKey)] = makeEnum("Key", kKeys, 0);
        params[globalParamIndex(kGlobalScale)] = makeEnum("Scale", kScaleNames, kScaleMinor);
        params[globalParamIndex(kGlobalReservedA)] = makeReserved();
        params[globalParamIndex(kGlobalReservedB)] = makeReserved();

        for (int t = 0; t < kNumTracks; ++t) {
            const TrackDefaults& d = kTrackDefaults[t];
            auto at = [&](int p) -> ParamDesc& { return params[trackParamIndex(t, p)]; };
            at(kTrackEnabled) = makeEnum("On", kOffOn, d.enabled, ParamType::Bool);
            at(kTrackMode) = makeEnum("Mode", kModes, d.mode);
            at(kTrackReserved) = makeReserved();
            at(kTrackLength) = makeInt("Length", 1, kMaxSteps, d.length);
            at(kTrackDivision) = makeEnum("Division", kDivisions, d.division);
            at(kTrackDirection) = makeEnum("Direction", kDirections, 0);
            at(kTrackOctave) = makeInt("Octave", 0, 8, d.octave);
            at(kTrackTranspose) = makeInt("Transpose", -24, 24, 0, " st", true);
            at(kTrackFollow) = makeEnum("Transpose input", kOffOn, 1, ParamType::Bool);
            at(kTrackGate) = makeInt("Gate", 2, 200, d.gate, " %");
            at(kTrackSwing) = makeInt("Swing", 10, 90, 50, " %");
            at(kTrackVelocity) = makeInt("Velocity", 10, 200, 100, " %");
            at(kTrackChance) = makeInt("Chance", 0, 100, 100, " %");
            at(kTrackSeed) = makeInt("Seed", 0, 9999, d.seed);
            at(kTrackDensity) = makeInt("Density", 0, 100, d.density, " %");
            at(kTrackRange) = makeInt("Range", 1, 24, d.range);
            at(kTrackVoices) = makeInt("Voices", 1, kMaxVoices, d.voices);
            at(kTrackRatchet) = makeInt("Ratchets", 0, 100, 0, " %");
            at(kTrackVariation) = makeInt("Variation", 0, 100, 0, " %");
            at(kTrackStart) = makeInt("Start", 1, kMaxSteps, 1);
            for (int p = 0; p < kNumTrackParams; ++p) {
                char name[32];
                std::snprintf(name, sizeof(name), "T%d %s", t + 1, at(p).name);
                std::snprintf(at(p).name, sizeof(at(p).name), "%s", name);
            }
        }
    }
};

int roundedInt(double v) { return int(std::lround(v)); }

} // namespace

const ParamDesc* paramTable()
{
    static const Table table;
    return table.params;
}

double clampParam(int index, double value)
{
    const ParamDesc& d = paramTable()[index];
    if (!std::isfinite(value))
        return d.def;
    return value < d.min ? d.min : (value > d.max ? d.max : value);
}

void defaultParamValues(double* values)
{
    const ParamDesc* table = paramTable();
    for (int i = 0; i < kNumParams; ++i)
        values[i] = table[i].def;
}

void formatParam(int index, double value, char* out, size_t size)
{
    const ParamDesc& d = paramTable()[index];
    const int v = roundedInt(clampParam(index, value));
    if (d.labels)
        std::snprintf(out, size, "%s", d.labels[v - int(d.min)]);
    else
        std::snprintf(out, size, d.showSign ? "%+d%s" : "%d%s", v, d.suffix);
}

double divisionBeats(int division)
{
    if (division < 0 || division >= kNumDivisions)
        division = kDefaultDivision;
    return kDivisionBeats[division];
}

int divisionForBeats(double beats)
{
    for (int i = 0; i < kNumDivisions; ++i)
        if (std::fabs(kDivisionBeats[i] - beats) < 1e-9)
            return i;
    return -1;
}

Settings settingsFromValues(const double* values)
{
    auto get = [&](int index) { return roundedInt(clampParam(index, values[index])); };

    Settings s{};
    s.global.key = get(globalParamIndex(kGlobalKey));
    s.global.scale = get(globalParamIndex(kGlobalScale));

    for (int t = 0; t < kNumTracks; ++t) {
        auto tp = [&](int p) { return get(trackParamIndex(t, p)); };
        TrackSettings& ts = s.tracks[t];
        ts.enabled = tp(kTrackEnabled) != 0;
        ts.mode = tp(kTrackMode);
        ts.start = tp(kTrackStart) - 1;
        ts.length = tp(kTrackLength);
        ts.stepBeats = divisionBeats(tp(kTrackDivision));
        ts.direction = tp(kTrackDirection);
        ts.octave = tp(kTrackOctave);
        ts.transpose = tp(kTrackTranspose);
        ts.followTranspose = tp(kTrackFollow) != 0;
        ts.gate = tp(kTrackGate);
        ts.swing = tp(kTrackSwing);
        ts.velocity = tp(kTrackVelocity);
        ts.chance = tp(kTrackChance);
        ts.gen.seed = tp(kTrackSeed);
        ts.gen.density = tp(kTrackDensity);
        ts.gen.range = tp(kTrackRange);
        ts.gen.voices = tp(kTrackVoices);
        ts.gen.ratchet = tp(kTrackRatchet);
        ts.gen.variation = tp(kTrackVariation);
    }
    return s;
}

int trackKey(const Settings& settings, int track, int degree, int liveTranspose)
{
    const TrackSettings& ts = settings.tracks[track];
    int key = 12 * (ts.octave + 1) + settings.global.key + degreeToSemitones(settings.global.scale, degree) +
              ts.transpose + liveTranspose;
    while (key > 127)
        key -= 12;
    while (key < 0)
        key += 12;
    return key;
}

} // namespace tetrad

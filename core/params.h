#pragma once
// Parameter table. The Rack parameter id of an entry is its index, so entries never move:
// reserved entries are unused and only keep the ids of saved patches stable.

#include "pattern.h"

#include <cstddef>

namespace tetrad {

enum class ParamType { Int, Bool, Enum, Reserved };

struct ParamDesc {
    char name[32];
    ParamType type;
    double min, max, def;
    const char* const* labels;   // Enum / Bool display names, null-terminated
    const char* suffix;          // Int display unit, may be ""
    bool showSign;               // Int: print "+3" instead of "3"
};

enum GlobalParam { kGlobalKey, kGlobalScale, kGlobalReservedA, kGlobalReservedB, kNumGlobalParams };

enum TrackParam {
    kTrackEnabled,
    kTrackMode,
    kTrackReserved,
    kTrackLength,
    kTrackDivision,
    kTrackDirection,
    kTrackOctave,
    kTrackTranspose,
    kTrackFollow,
    kTrackGate,
    kTrackSwing,
    kTrackVelocity,
    kTrackChance,
    kTrackSeed,
    kTrackDensity,
    kTrackRange,
    kTrackVoices,
    kTrackRatchet,
    kTrackVariation,
    kTrackStart,   // first step of the loop window, 1-based
    kNumTrackParams
};

constexpr int kNumParams = kNumGlobalParams + kNumTracks * kNumTrackParams;

enum Mode { kModeMono, kModePoly };
enum Direction { kDirForward, kDirBackward, kDirPingPong, kDirRandom, kDirDrunk };

constexpr int globalParamIndex(int p) { return p; }
constexpr int trackParamIndex(int track, int p) { return kNumGlobalParams + track * kNumTrackParams + p; }

const ParamDesc* paramTable();   // kNumParams entries
double clampParam(int index, double value);
void defaultParamValues(double* values);
void formatParam(int index, double value, char* out, size_t size);

double divisionBeats(int division);   // step length in quarter notes
int divisionForBeats(double beats);   // -1 if no division has that length

struct TrackSettings {
    bool enabled;
    int mode;
    int start;        // 0..kMaxSteps-1
    int length;       // 1..kMaxSteps
    double stepBeats;
    int direction;
    int octave;
    int transpose;
    bool followTranspose;
    int gate;         // %
    int swing;        // 10..90 % (50 = straight)
    int velocity;     // % scaling of step velocity
    int chance;       // % scaling of step probability
    GenKey gen;
};

struct GlobalSettings {
    int key;          // 0..11, C = 0
    int scale;
};

struct Settings {
    GlobalSettings global;
    TrackSettings tracks[kNumTracks];
};

Settings settingsFromValues(const double* values);

// MIDI key (60 = C4) of a scale degree on a track, plus a live transposition in semitones,
// folded by octaves into 0..127.
int trackKey(const Settings& settings, int track, int degree, int liveTranspose = 0);

} // namespace tetrad

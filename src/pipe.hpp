#pragma once
// Tetrad -> Tetrad Pipe expander link. Tetrad writes one message per sample into the Pipe placed
// directly on its right (Rack's double-buffered expander messages: one sample of latency).

#include "../core/cv.h"

#include <string>

struct PipeMessage {
    tetrad::CvFrame frame;
    bool clock;   // 1 ms trigger on every clock pulse received by Tetrad
    bool reset;   // 1 ms trigger on reset and when playback starts
    // New on every message: a bypassed Tetrad stops sending, and the Pipe must not hold the
    // last frame (its gates would hang).
    uint32_t serial;
};

namespace tetrad::pipe {

constexpr int kOutputs = 16;
constexpr int kTracks = tetrad::kNumTracks;
constexpr int kRows = tetrad::kDrumRows;   // grid rows = scale degrees 0..7 = drum rows

// What an output carries. Values are saved in patches: append only.
enum Signal {
    kNone,
    kPitch,         // V/oct, polyphonic (all voices of the track)
    kVelocity,      // 0-10 V, polyphonic
    kGate,          // polyphonic
    kTrigger,       // 1 ms at every note start of the track
    kRowGate,       // one grid row (drum row) of the track
    kRowTrigger,
    kRowVelocity,
    kClock,
    kReset,
    kSignalCount
};
extern const char* const kSignalNames[kSignalCount];

struct Route {
    int signal = kNone;
    int track = 0;   // 0..3
    int row = 0;     // 0..7, row 1 = bottom grid row at the default scroll
};

inline bool usesTrack(int s) { return s != kNone && s != kClock && s != kReset; }
inline bool usesRow(int s) { return s == kRowGate || s == kRowTrigger || s == kRowVelocity; }

std::string routeName(const Route& r, bool shortName);
Route defaultRoute(int output);

} // namespace tetrad::pipe

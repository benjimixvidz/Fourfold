#pragma once
// Pattern data: what each track plays, step by step.

#include <cstdint>

namespace tetrad {

constexpr int kNumTracks = 4;
constexpr int kMaxSteps = 128;   // 8 pages of 16
constexpr int kMaxVoices = 7;    // notes per step in Poly mode
constexpr int kMaxRatchet = 8;
constexpr uint8_t kGateTie = 200;   // a step gate above 100 % ties into the next step
constexpr int kDrumRows = 8;        // Drum mode: one fixed voice per grid row (row 1 at the bottom)

// Trig conditions, evaluated each time the step comes up.
enum Condition : uint8_t {
    kCondAlways,
    kCondAB,        // plays on loop A of every B loops
    kCondNotAB,
    kCondFirst,     // only during the first loop after playback starts
    kCondNotFirst,
    kCondPre,       // only if the previous conditional step of the track played
    kCondNotPre,
    kCondCount
};

// One row's hit in a step of a Drum track: the same fields as a step.
struct DrumHit {
    uint8_t velocity = 100;
    uint8_t gate = 100;          // 1..100 or kGateTie
    uint8_t probability = 100;
    uint8_t ratchet = 1;
    uint8_t condition = kCondAlways;
    uint8_t condA = 1;
    uint8_t condB = 1;
};

struct Step {
    bool active = false;
    uint8_t numNotes = 1;            // 1..kMaxVoices (Poly mode plays all of them)
    int8_t degree[kMaxVoices] = {0, 2, 4, 6, 8, 10, 12};   // scale degrees relative to the root
    uint8_t velocity = 75;           // 1..127
    uint8_t gate = 100;              // % of the track gate, 1..100 or kGateTie
    uint8_t probability = 100;       // 0..100
    uint8_t ratchet = 1;             // 1..kMaxRatchet hits spread over the step
    uint8_t condition = kCondAlways;
    uint8_t condA = 1;               // A:B conditions, 1 <= A <= B <= 8
    uint8_t condB = 1;
    // Drum mode (the fields above serve Mono and Poly): a bit per row with a hit, and the hits.
    uint8_t hits = 0;
    DrumHit drum[kDrumRows];
};

// Drum mode, per row of the track: scales applied to every hit of the row, and a mute.
struct RowSettings {
    uint8_t level = 100;         // % of the hits' velocity
    uint8_t probability = 100;   // % of the hits' probability
    bool mute = false;
};

// Generator settings a pattern was produced from. The sequencer regenerates a track only when
// the current settings differ from the key stored with its pattern, so hand edits survive as long
// as these settings don't move.
struct GenKey {
    int seed = -1;
    int density = 0;
    int range = 1;
    int voices = 1;
    int ratchet = 0;     // % of active steps that get a ratchet
    int variation = 0;   // % of active steps that get a trig condition

    bool operator==(const GenKey& o) const
    {
        return seed == o.seed && density == o.density && range == o.range && voices == o.voices &&
               ratchet == o.ratchet && variation == o.variation;
    }
    bool operator!=(const GenKey& o) const { return !(*this == o); }
};

struct TrackPattern {
    Step steps[kMaxSteps];
    RowSettings rows[kDrumRows];
    GenKey key;
};

struct PatternBank {
    TrackPattern tracks[kNumTracks];
};

// Deterministic: the same key always yields the same pattern. It writes both the notes (Mono,
// Poly) and the drum hits (Drum), from separate random streams; the row settings are kept.
void generatePattern(const GenKey& key, TrackPattern& out);

} // namespace tetrad

#pragma once
// 16x8 pad grid model: what each pad shows and what pressing it does to the pattern.
//
// Columns are the 16 steps of the current page (8 pages = 128 steps). Rows depend on the lane:
//  - Note: 8 consecutive scale degrees, highest on top (Mono: one note per step, Poly: up to 7);
//  - Velocity, Gate, Probability, Ratchet: an 8-level bar graph per step;
//  - Condition: one choice per row (Always at the bottom).

#include "params.h"
#include "pattern.h"

#include <cstdint>

namespace tetrad {

constexpr int kGridCols = 16;
constexpr int kGridRows = 8;
constexpr int kGridPages = kMaxSteps / kGridCols;

enum Lane { kLaneNote, kLaneVelocity, kLaneGate, kLaneProbability, kLaneRatchet, kLaneCondition, kLaneCount };
extern const char* const kLaneNames[kLaneCount];

struct GridView {
    int track = 0;
    int page = 0;
    int lane = kLaneNote;
    int lowestDegree = 0;   // Note lane: degree shown on the bottom row
};

struct PadInfo {
    bool lit;        // the pad holds a value
    bool active;     // the step plays
    bool inLoop;     // the step is inside the track's Start/Length window
    bool rootRow;    // Note lane: the row is a root (degree multiple of the scale size)
};

// Smallest and largest bottom-row degree, so every visible degree fits the pattern storage.
constexpr int kGridMinDegree = -24;
constexpr int kGridMaxDegree = 40;

int stepForColumn(const GridView& view, int col);
PadInfo padInfo(const PatternBank& bank, const GridView& view, bool poly, int scaleSize, int start, int length,
                int col, int row);
// Returns true if the pattern changed. Row 0 is the top row.
bool pressPad(PatternBank& bank, const GridView& view, bool poly, int col, int row);
// Short label of the value a row stands for in the current lane ("127", "1:4", "x3"...).
const char* rowLabel(const GridView& view, int row, char* buf, int size);

} // namespace tetrad

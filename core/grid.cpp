#include "grid.h"

#include <algorithm>
#include <cstdio>

namespace tetrad {

const char* const kLaneNames[kLaneCount] = {"NOTE", "VEL", "GATE", "PROB", "RTCH", "COND"};

namespace {

// Bar-graph levels, bottom row first.
const int kVelocityLevels[kGridRows] = {16, 32, 48, 64, 80, 96, 112, 127};
const int kGateLevels[kGridRows] = {12, 25, 37, 50, 62, 75, 100, kGateTie};
const int kProbabilityLevels[kGridRows] = {12, 25, 37, 50, 62, 75, 87, 100};

struct ConditionChoice {
    uint8_t condition, a, b;
    const char* label;
};
const ConditionChoice kConditionChoices[kGridRows] = {
    {kCondAlways, 1, 1, "--"}, {kCondAB, 1, 2, "1:2"}, {kCondAB, 2, 2, "2:2"}, {kCondAB, 1, 4, "1:4"},
    {kCondAB, 2, 4, "2:4"},    {kCondAB, 3, 4, "3:4"}, {kCondAB, 4, 4, "4:4"}, {kCondFirst, 1, 1, "1st"},
};

int levelIndex(int row) { return kGridRows - 1 - row; }   // 0 = bottom row

int rowDegree(const GridView& view, int row) { return view.lowestDegree + levelIndex(row); }

bool hasDegree(const Step& s, int degree, bool poly)
{
    const int n = poly ? std::clamp(int(s.numNotes), 1, kMaxVoices) : 1;
    return std::find(s.degree, s.degree + n, degree) != s.degree + n;
}

template <typename T>
bool matchesChoice(const T& s, const ConditionChoice& c)
{
    if (s.condition != c.condition)
        return false;
    return c.condition != kCondAB || (s.condA == c.a && s.condB == c.b);
}

// Bar-graph lanes, on a step (Mono, Poly) or a drum hit: what a level shows, what a press sets.
template <typename T>
bool levelLit(const T& s, int lane, int level)
{
    switch (lane) {
    case kLaneVelocity: return s.velocity >= kVelocityLevels[level];
    case kLaneGate: return s.gate >= kGateLevels[level];
    case kLaneProbability: return s.probability >= kProbabilityLevels[level];
    case kLaneRatchet: return s.ratchet >= level + 1;
    case kLaneCondition: return matchesChoice(s, kConditionChoices[level]);
    default: return false;
    }
}

template <typename T>
bool setLevel(T& s, int lane, int level)
{
    switch (lane) {
    case kLaneVelocity: s.velocity = uint8_t(kVelocityLevels[level]); return true;
    case kLaneGate: s.gate = uint8_t(kGateLevels[level]); return true;
    case kLaneProbability: s.probability = uint8_t(kProbabilityLevels[level]); return true;
    case kLaneRatchet: s.ratchet = uint8_t(level + 1); return true;
    case kLaneCondition: {
        const ConditionChoice& c = kConditionChoices[level];
        s.condition = c.condition;
        s.condA = c.a;
        s.condB = c.b;
        return true;
    }
    default: return false;
    }
}

} // namespace

int stepForColumn(const GridView& view, int col)
{
    return std::clamp(view.page, 0, kGridPages - 1) * kGridCols + std::clamp(col, 0, kGridCols - 1);
}

PadInfo padInfo(const PatternBank& bank, const GridView& view, int mode, int scaleSize, int start, int length,
                int col, int row)
{
    const int index = stepForColumn(view, col);
    const Step& s = bank.tracks[std::clamp(view.track, 0, kNumTracks - 1)].steps[index];
    const int level = levelIndex(row);

    PadInfo info{};
    info.inLoop = (index - start + kMaxSteps) % kMaxSteps < length;
    if (mode == kModeDrum) {
        const int focus = std::clamp(view.focusRow, 0, kDrumRows - 1);
        if (view.lane == kLaneNote) {
            info.lit = info.active = s.hits & (1u << level);
        } else {
            info.active = s.hits & (1u << focus);
            info.lit = levelLit(s.drum[focus], view.lane, level);
        }
        return info;
    }
    info.active = s.active;
    if (view.lane == kLaneNote) {
        const int degree = rowDegree(view, row);
        info.lit = s.active && hasDegree(s, degree, mode == kModePoly);
        const int size = scaleSize > 0 ? scaleSize : 7;
        info.rootRow = ((degree % size) + size) % size == 0;
    } else {
        info.lit = levelLit(s, view.lane, level);
    }
    return info;
}

bool pressPad(PatternBank& bank, const GridView& view, int mode, int col, int row)
{
    if (col < 0 || col >= kGridCols || row < 0 || row >= kGridRows)
        return false;
    Step& s = bank.tracks[std::clamp(view.track, 0, kNumTracks - 1)].steps[stepForColumn(view, col)];
    const int level = levelIndex(row);
    const bool poly = mode == kModePoly;

    if (mode == kModeDrum) {
        if (view.lane == kLaneNote) {
            s.hits ^= uint8_t(1u << level);
            return true;
        }
        return setLevel(s.drum[std::clamp(view.focusRow, 0, kDrumRows - 1)], view.lane, level);
    }
    switch (view.lane) {
    case kLaneNote: {
        const int degree = std::clamp(rowDegree(view, row), -100, 100);
        if (!poly) {
            if (s.active && s.degree[0] == degree) {
                s.active = false;
            } else {
                s.active = true;
                s.degree[0] = int8_t(degree);
            }
            return true;
        }
        int n = std::clamp(int(s.numNotes), 1, kMaxVoices);
        if (!s.active) {
            s.active = true;
            s.numNotes = 1;
            s.degree[0] = int8_t(degree);
            return true;
        }
        int8_t* end = s.degree + n;
        int8_t* found = std::find(s.degree, end, degree);
        if (found != end) {
            // Remove the note; the last one leaves the step silent (and keeps it for undo-by-click).
            if (n == 1) {
                s.active = false;
                return true;
            }
            std::rotate(found, found + 1, end);
            s.numNotes = uint8_t(n - 1);
            return true;
        }
        if (n == kMaxVoices) {   // an 8th note replaces the oldest one
            std::rotate(s.degree, s.degree + 1, end);
            --n;
        }
        s.degree[n] = int8_t(degree);
        s.numNotes = uint8_t(n + 1);
        return true;
    }
    default:
        return setLevel(s, view.lane, level);
    }
}

const char* rowLabel(const GridView& view, int mode, int row, char* buf, int size)
{
    const int level = levelIndex(row);
    switch (view.lane) {
    case kLaneNote:
        if (mode == kModeDrum)
            std::snprintf(buf, size_t(size), "R%d", level + 1);
        else
            std::snprintf(buf, size_t(size), "%+d", rowDegree(view, row));
        break;
    case kLaneVelocity:
        std::snprintf(buf, size_t(size), "%d", kVelocityLevels[level]);
        break;
    case kLaneGate:
        if (kGateLevels[level] > 100)
            std::snprintf(buf, size_t(size), "tie");
        else
            std::snprintf(buf, size_t(size), "%d%%", kGateLevels[level]);
        break;
    case kLaneProbability:
        std::snprintf(buf, size_t(size), "%d%%", kProbabilityLevels[level]);
        break;
    case kLaneRatchet:
        std::snprintf(buf, size_t(size), "x%d", level + 1);
        break;
    case kLaneCondition:
        std::snprintf(buf, size_t(size), "%s", kConditionChoices[level].label);
        break;
    default:
        buf[0] = '\0';
    }
    return buf;
}

} // namespace tetrad

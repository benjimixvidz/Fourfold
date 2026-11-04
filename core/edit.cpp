#include "edit.h"

#include <algorithm>
#include <cstring>

namespace tetrad {

namespace {

int wrap(int i) { return ((i % kMaxSteps) + kMaxSteps) % kMaxSteps; }

const char* const kConditionNames[kCondCount] = {"ALWAYS", "A:B", "NOT A:B", "1ST", "NOT 1ST", "PRE", "NOT PRE"};

// Applies `fn` to every selected step; true if one of them changed.
template <typename F>
bool forSelected(PatternBank& bank, int track, const StepMask& sel, F&& fn)
{
    bool changed = false;
    for (int i = 0; i < kMaxSteps; ++i) {
        if (!sel[size_t(i)])
            continue;
        Step& s = bank.tracks[track].steps[i];
        const Step before = s;
        fn(s);
        changed |= std::memcmp(&before, &s, sizeof(Step)) != 0;
    }
    return changed;
}

} // namespace

const char* conditionName(int condition)
{
    return condition >= 0 && condition < kCondCount ? kConditionNames[condition] : "?";
}

int stepField(const Step& s, int field)
{
    switch (field) {
    case kStepVelocity: return s.velocity;
    case kStepGate: return s.gate;
    case kStepProbability: return s.probability;
    case kStepRatchet: return s.ratchet;
    case kStepCondition: return s.condition;
    case kStepCondA: return s.condA;
    case kStepCondB: return s.condB;
    case kStepNotes: return s.numNotes;
    default: return 0;
    }
}

void setStepField(Step& s, int field, int v)
{
    switch (field) {
    case kStepVelocity: s.velocity = uint8_t(std::clamp(v, 1, 127)); break;
    case kStepGate: s.gate = v > 100 ? kGateTie : uint8_t(std::clamp(v, 1, 100)); break;
    case kStepProbability: s.probability = uint8_t(std::clamp(v, 0, 100)); break;
    case kStepRatchet: s.ratchet = uint8_t(std::clamp(v, 1, kMaxRatchet)); break;
    case kStepCondition:
        s.condition = uint8_t(std::clamp(v, 0, kCondCount - 1));
        // A:B over a single loop means nothing.
        if ((s.condition == kCondAB || s.condition == kCondNotAB) && s.condB < 2) {
            s.condA = 1;
            s.condB = 2;
        }
        break;
    case kStepCondA: s.condA = uint8_t(std::clamp(v, 1, int(s.condB))); break;
    case kStepCondB:
        s.condB = uint8_t(std::clamp(v, 1, 8));
        s.condA = std::min(s.condA, s.condB);
        break;
    case kStepNotes: s.numNotes = uint8_t(std::clamp(v, 1, kMaxVoices)); break;
    default: break;
    }
}

bool setSteps(PatternBank& bank, int track, const StepMask& sel, int field, int value)
{
    return forSelected(bank, track, sel, [&](Step& s) { setStepField(s, field, value); });
}

bool adjustSteps(PatternBank& bank, int track, const StepMask& sel, int field, int delta)
{
    if (delta == 0)
        return false;
    return forSelected(bank, track, sel, [&](Step& s) {
        if (field != kStepGate)
            setStepField(s, field, stepField(s, field) + delta);
        else if (s.gate > 100)
            s.gate = delta < 0 ? 100 : s.gate;
        else
            s.gate = s.gate == 100 && delta > 0 ? kGateTie : uint8_t(std::clamp(s.gate + delta, 1, 100));
    });
}

bool resetSteps(PatternBank& bank, int track, const StepMask& sel, int field)
{
    const Step def;
    return forSelected(bank, track, sel, [&](Step& s) {
        if (field == kStepCondition || field == kStepCondA || field == kStepCondB) {
            s.condition = def.condition;
            s.condA = def.condA;
            s.condB = def.condB;
        } else {
            setStepField(s, field, stepField(def, field));
        }
    });
}

bool transposeSteps(PatternBank& bank, int track, const StepMask& sel, int degrees)
{
    return forSelected(bank, track, sel, [&](Step& s) {
        for (int8_t& d : s.degree)
            d = int8_t(std::clamp(d + degrees, -100, 100));
    });
}

StepMask windowMask(int start, int length)
{
    StepMask m;
    length = std::clamp(length, 0, kMaxSteps);
    for (int i = 0; i < length; ++i)
        m.set(size_t(wrap(start + i)));
    return m;
}

StepMask rangeMask(int first, int last)
{
    StepMask m;
    first = std::clamp(first, 0, kMaxSteps - 1);
    last = std::clamp(last, 0, kMaxSteps - 1);
    if (first > last)
        std::swap(first, last);
    for (int i = first; i <= last; ++i)
        m.set(size_t(i));
    return m;
}

int firstStep(const StepMask& m)
{
    for (int i = 0; i < kMaxSteps; ++i)
        if (m[size_t(i)])
            return i;
    return -1;
}

void copySteps(const PatternBank& bank, int track, const StepMask& sel, Clipboard& out)
{
    out = Clipboard{};
    const int first = firstStep(sel);
    if (first < 0)
        return;
    int last = kMaxSteps - 1;
    while (!sel[size_t(last)])
        --last;
    out.count = last - first + 1;
    for (int i = 0; i < out.count; ++i) {
        out.steps[i] = bank.tracks[track].steps[first + i];
        out.used[size_t(i)] = sel[size_t(first + i)];
    }
}

StepMask pasteSteps(PatternBank& bank, int track, int at, const Clipboard& clip)
{
    StepMask written;
    for (int i = 0; i < clip.count; ++i)
        if (clip.used[size_t(i)]) {
            const int dst = wrap(at + i);
            bank.tracks[track].steps[dst] = clip.steps[i];
            written.set(size_t(dst));
        }
    return written;
}

void clearSteps(PatternBank& bank, int track, const StepMask& sel)
{
    for (int i = 0; i < kMaxSteps; ++i)
        if (sel[size_t(i)])
            bank.tracks[track].steps[i] = Step{};
}

void shiftSteps(PatternBank& bank, int track, const StepMask& sel, int amount)
{
    int pos[kMaxSteps];
    Step data[kMaxSteps];
    int n = 0;
    for (int i = 0; i < kMaxSteps; ++i)
        if (sel[size_t(i)]) {
            pos[n] = i;
            data[n++] = bank.tracks[track].steps[i];
        }
    if (n < 2)
        return;
    for (int i = 0; i < n; ++i)
        bank.tracks[track].steps[pos[((i + amount) % n + n) % n]] = data[i];
}

void euclid(PatternBank& bank, int track, int start, int length, int pulses, int rotation, int degree)
{
    length = std::clamp(length, 1, kMaxSteps);
    pulses = std::clamp(pulses, 0, length);
    for (int i = 0; i < length; ++i) {
        // Bresenham distribution: the classic Euclidean rhythm up to rotation.
        const int j = ((i + rotation) % length + length) % length;
        const bool hit = pulses > 0 && (j * pulses) % length < pulses;
        Step& s = bank.tracks[track].steps[wrap(start + i)];
        s.active = hit;
        if (hit) {
            s.numNotes = 1;
            s.degree[0] = int8_t(std::clamp(degree, -100, 100));
            s.probability = std::max<uint8_t>(s.probability, 1);
        }
    }
}

bool expandSteps(PatternBank& bank, int track, int start, int length)
{
    length = std::clamp(length, 1, kMaxSteps);
    if (length * 2 > kMaxSteps)
        return false;
    Step src[kMaxSteps];
    for (int i = 0; i < length; ++i)
        src[i] = bank.tracks[track].steps[wrap(start + i)];
    for (int i = 0; i < length; ++i) {
        bank.tracks[track].steps[wrap(start + 2 * i)] = src[i];
        bank.tracks[track].steps[wrap(start + 2 * i + 1)] = Step{};
    }
    return true;
}

void condenseSteps(PatternBank& bank, int track, int start, int length)
{
    length = std::clamp(length, 1, kMaxSteps);
    const int half = (length + 1) / 2;
    for (int i = 0; i < half; ++i)
        bank.tracks[track].steps[wrap(start + i)] = bank.tracks[track].steps[wrap(start + 2 * i)];
    for (int i = half; i < length; ++i)
        bank.tracks[track].steps[wrap(start + i)] = Step{};
}

void reverseSteps(PatternBank& bank, int track, int start, int length)
{
    length = std::clamp(length, 1, kMaxSteps);
    for (int i = 0; i < length / 2; ++i)
        std::swap(bank.tracks[track].steps[wrap(start + i)], bank.tracks[track].steps[wrap(start + length - 1 - i)]);
}

} // namespace tetrad

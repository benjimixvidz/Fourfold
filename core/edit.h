#pragma once
// Pattern editing operations: step fields, selections, clipboard, loop tools, Euclidean fill and
// transposition. Plain functions on a PatternBank, so the UI can undo any of them by keeping a
// copy of the bank.

#include "pattern.h"

#include <bitset>

namespace tetrad {

using StepMask = std::bitset<kMaxSteps>;

enum StepField {
    kStepVelocity,
    kStepGate,
    kStepProbability,
    kStepRatchet,
    kStepCondition,
    kStepCondA,
    kStepCondB,
    kStepNotes,
};

const char* conditionName(int condition);

int stepField(const Step& s, int field);
// Clamped to the field's range. A gate above 100 is a tie; A:B conditions keep 1 <= A <= B and
// start from 1:2.
void setStepField(Step& s, int field, int value);

// Each function on selected steps returns true if a step changed.
bool setSteps(PatternBank& bank, int track, const StepMask& sel, int field, int value);
// Relative edit; the gate stops at 100 % before the tie and leaves the tie back to 100 %.
bool adjustSteps(PatternBank& bank, int track, const StepMask& sel, int field, int delta);
// Back to the default value; the three condition fields reset together.
bool resetSteps(PatternBank& bank, int track, const StepMask& sel, int field);
// Moves every note by `degrees` scale degrees.
bool transposeSteps(PatternBank& bank, int track, const StepMask& sel, int degrees);

// Steps [start, start + length) of the loop window, wrapping at 128.
StepMask windowMask(int start, int length);
StepMask rangeMask(int first, int last);   // inclusive, in either order
int firstStep(const StepMask& m);           // -1 if empty

// Copied steps keep their relative positions (gaps included), from the first selected step.
struct Clipboard {
    int count = 0;   // span from the first to the last copied step
    Step steps[kMaxSteps];
    StepMask used;   // which positions of the span were selected
};

void copySteps(const PatternBank& bank, int track, const StepMask& sel, Clipboard& out);
// Pastes at `at` (wrapping); returns the mask of written steps.
StepMask pasteSteps(PatternBank& bank, int track, int at, const Clipboard& clip);
void clearSteps(PatternBank& bank, int track, const StepMask& sel);

// Rotates the step data among the selected positions by `amount`.
void shiftSteps(PatternBank& bank, int track, const StepMask& sel, int amount);

// Euclidean rhythm over the window: `pulses` hits spread over `length` steps, rotated.
// Hits get `degree`; the other window steps are turned off.
void euclid(PatternBank& bank, int track, int start, int length, int pulses, int rotation, int degree);

// Spreads the window's steps to every other step, so the caller can double the length and halve
// the division without changing the music. Returns false when the doubled window would not fit.
bool expandSteps(PatternBank& bank, int track, int start, int length);
// Keeps every other step of the window in its first half (destructive).
void condenseSteps(PatternBank& bank, int track, int start, int length);
void reverseSteps(PatternBank& bank, int track, int start, int length);

} // namespace tetrad

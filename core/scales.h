#pragma once
// Musical scales: a step stores scale degrees, which are mapped to semitones here.
// Changing the scale therefore re-harmonises an existing pattern.

namespace tetrad {

// Indices are saved in patches: append only.
enum Scale : int {
    kScaleChromatic,
    kScaleMajor,
    kScaleMinor,
    kScaleDorian,
    kScalePhrygian,
    kScaleLydian,
    kScaleMixolydian,
    kScaleLocrian,
    kScaleHarmonicMinor,
    kScaleMelodicMinor,
    kScalePentaMajor,
    kScalePentaMinor,
    kScaleBlues,
    kScaleWholeTone,
    kScalePersian,
    kScaleDiminished,
    kScalePhrygianDominant,
    kScaleByzantine,
    kScaleNeapolitanMinor,
    kScaleNeapolitanMajor,
    kScaleUkrainianDorian,
    kScaleHungarianMinor,
    kScaleHungarianMajor,
    kScaleOriental,
    kScaleRagaTodi,
    kScaleBalinese,
    kScaleChinese,
    kScaleJapaneseIn,
    kScaleJapaneseYo,
    kScaleCount
};

extern const char* const kScaleNames[kScaleCount + 1];

// Degree 0 is the root; negative degrees and degrees beyond the scale size wrap by octaves.
int degreeToSemitones(int scale, int degree);

// Number of notes per octave in the scale.
int scaleSize(int scale);

// "C4", "D#3"... (MIDI 60 = C4).
const char* noteName(int key, char* out, int size);

} // namespace tetrad

#include "scales.h"

#include <cstdio>

namespace tetrad {

const char* const kScaleNames[kScaleCount + 1] = {
    "Chromatic",  "Major",         "Minor",         "Dorian",      "Phrygian",
    "Lydian",     "Mixolydian",    "Locrian",       "Harm. minor", "Mel. minor",
    "Penta major", "Penta minor",  "Blues",         "Whole tone",  "Persian",
    "Diminished", "Phryg. dominant", "Byzantine",   "Neapol. minor", "Neapol. major",
    "Ukr. dorian", "Hung. minor",  "Hung. major",   "Oriental",    "Raga Todi",
    "Balinese",   "Chinese",       "Japanese In",   "Japanese Yo", nullptr,
};

namespace {

struct ScaleDef {
    int size;
    int semis[12];
};

const ScaleDef kScales[] = {
    {12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {7, {0, 2, 4, 5, 7, 9, 11}},
    {7, {0, 2, 3, 5, 7, 8, 10}},
    {7, {0, 2, 3, 5, 7, 9, 10}},
    {7, {0, 1, 3, 5, 7, 8, 10}},
    {7, {0, 2, 4, 6, 7, 9, 11}},
    {7, {0, 2, 4, 5, 7, 9, 10}},
    {7, {0, 1, 3, 5, 6, 8, 10}},
    {7, {0, 2, 3, 5, 7, 8, 11}},
    {7, {0, 2, 3, 5, 7, 9, 11}},
    {5, {0, 2, 4, 7, 9}},
    {5, {0, 3, 5, 7, 10}},
    {6, {0, 3, 5, 6, 7, 10}},
    {6, {0, 2, 4, 6, 8, 10}},
    {7, {0, 1, 4, 5, 6, 8, 11}},
    {8, {0, 2, 3, 5, 6, 8, 9, 11}},
    {7, {0, 1, 4, 5, 7, 8, 10}},
    {7, {0, 1, 4, 5, 7, 8, 11}},
    {7, {0, 1, 3, 5, 7, 8, 11}},
    {7, {0, 1, 3, 5, 7, 9, 11}},
    {7, {0, 2, 3, 6, 7, 9, 10}},
    {7, {0, 2, 3, 6, 7, 8, 11}},
    {7, {0, 3, 4, 6, 7, 9, 10}},
    {7, {0, 1, 4, 5, 6, 9, 10}},
    {7, {0, 1, 3, 6, 7, 8, 11}},
    {5, {0, 1, 3, 7, 8}},
    {5, {0, 4, 6, 7, 11}},
    {5, {0, 1, 5, 7, 8}},
    {5, {0, 2, 5, 7, 9}},
};
static_assert(sizeof(kScales) / sizeof(kScales[0]) == kScaleCount, "one definition per scale");

} // namespace

int degreeToSemitones(int scale, int degree)
{
    if (scale < 0 || scale >= kScaleCount)
        scale = kScaleChromatic;
    const ScaleDef& s = kScales[scale];
    int octave = degree / s.size;
    int index = degree % s.size;
    if (index < 0) {
        index += s.size;
        --octave;
    }
    return octave * 12 + s.semis[index];
}

int scaleSize(int scale)
{
    return kScales[(scale < 0 || scale >= kScaleCount) ? kScaleChromatic : scale].size;
}

const char* noteName(int key, char* out, int size)
{
    static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int k = key < 0 ? 0 : (key > 127 ? 127 : key);
    std::snprintf(out, size_t(size), "%s%d", names[k % 12], k / 12 - 1);
    return out;
}

} // namespace tetrad

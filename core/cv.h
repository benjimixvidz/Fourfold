#pragma once
// Clock and CV building blocks, sample by sample and allocation-free:
//  - ClockFollower turns clock pulses into the transport the sequencer expects;
//  - CvVoices turns note events into voices (pitch, gate, velocity) with retriggers, plus
//    per-track triggers and 8 drum rows per track (one gate + velocity per scale degree 0..7,
//    i.e. per grid row at the default scroll).

#include "params.h"
#include "pattern.h"
#include "sequencer.h"

#include <cstdint>

namespace tetrad {

// Every rising clock edge is 1/ppqn of a quarter note. Between edges the position moves on
// with the last measured period but stops just short of the next pulse, so a step that falls
// on a pulse fires exactly with it even if the clock slows down or stops.
class ClockFollower {
public:
    void reset()                               // the next pulse is beat 0
    {
        pulses_ = -1;
        baseBeat_ = 0.0;
    }
    // A new resolution keeps the position of the pulses already received; only the next ones
    // count at the new resolution.
    void setPpqn(int ppqn);

    // Call once per sample, `pulse` being true on the sample of a rising clock edge.
    // `run` false stops playback (notes off) without losing the position.
    TransportState tick(bool pulse, bool run, double sampleRate);

private:
    int ppqn_ = 4;
    int64_t pulses_ = -1;            // since the last reset or PPQN change; -1: waiting for the first
    double baseBeat_ = 0.0;          // beat of pulse 0 of the count (a PPQN change moves it)
    double samplesSincePulse_ = 0;
    double periodSamples_ = 0;       // 0 until two pulses have been seen
    // Derived from the period and ppqn, recomputed on each pulse instead of each sample.
    int derivedPpqn_ = 0;
    double beatsPerSample_ = 0;
    double hold_ = 0;
    double tempo_ = 120.0;
};

constexpr int kMaxChannels = kDrumRows;   // output channels of a track (Poly: 7, Drum: 8)

// Everything the module outputs for one sample, in a compact form (the expander message).
struct CvFrame {
    uint8_t channels[kNumTracks];
    uint8_t key[kNumTracks][kMaxChannels];
    uint8_t velocity[kNumTracks][kMaxChannels];   // 0..127
    uint8_t gateMask[kNumTracks];               // bit per voice, retrigger gaps applied
    uint8_t trigMask;                           // bit per track: a note started < 1 ms ago
    uint8_t rowGate[kNumTracks];               // bit per row
    uint8_t rowTrig[kNumTracks];               // bit per row
    uint8_t rowVelocity[kNumTracks][kDrumRows];
};

class CvVoices final : public NoteSink {
public:
    struct Voice {
        int key = 60;
        float velocity = 0.f;
        bool gate = false;
        bool gateBefore = false;   // gate at the end of the previous sample
        int retrig = 0;            // samples left with the gate forced low
        uint32_t age = 0;
    };

    void clear();
    // Mono: one voice; Poly: up to 7, allocated by key; Drum: 8, channel n = row n.
    void setMode(int track, int mode)
    {
        const bool poly = mode == kModePoly, drum = mode == kModeDrum;
        frameDirty_ |= poly_[track] != poly || drum_[track] != drum;
        poly_[track] = poly;
        drum_[track] = drum;
    }
    void setRetrigSamples(int n) { retrigSamples_ = n > 0 ? n : 1; }

    // Call before rendering each sample.
    void beginSample();
    void emit(const NoteEvent& e) override;

    // The whole sample (gates, triggers, drum rows): once per sample after rendering, always with the
    // same frame, which is only rewritten on change.
    void output(CvFrame& frame);

    // Per-voice access; gateOut consumes one sample of retrigger gap, so use it instead of output().
    int channels(int track) const { return drum_[track] ? kDrumRows : poly_[track] ? channels_[track] : 1; }
    const Voice& voice(int track, int channel) const { return voices_[track][channel]; }
    bool gateOut(int track, int channel);

private:

    Voice voices_[kNumTracks][kMaxChannels];
    int channels_[kNumTracks] = {1, 1, 1, 1};
    bool poly_[kNumTracks] = {};
    bool drum_[kNumTracks] = {};
    int retrigSamples_ = 48;
    uint32_t counter_ = 0;
    bool changed_ = false;   // an event arrived since the last beginSample()

    struct DrumRow {
        bool gate = false;
        bool gateBefore = false;
        int retrig = 0;
        int trig = 0;
        int key = -1;   // the note holding the row: an older note's off leaves it open
        uint8_t velocity = 0;
    };
    DrumRow rows_[kNumTracks][kDrumRows];
    int trig_[kNumTracks] = {};
    // output() fast path: the previous frame still holds when no event came in and no
    // retrigger/trigger countdown is running.
    bool frameDirty_ = true;
    bool countdowns_ = false;
};

} // namespace tetrad

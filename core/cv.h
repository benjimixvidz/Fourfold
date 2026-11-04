#pragma once
// Clock and CV building blocks, sample by sample and allocation-free:
//  - ClockFollower turns clock pulses into the transport the sequencer expects;
//  - CvVoices turns note events into voices (pitch, gate, velocity) with retriggers, plus
//    per-track triggers and 8 drum lanes per track (one gate + velocity per scale degree 0..7,
//    i.e. per grid row at the default scroll).

#include "pattern.h"
#include "sequencer.h"

#include <cstdint>

namespace tetrad {

// Every rising clock edge is 1/ppqn of a quarter note. Between edges the position moves on
// with the last measured period but stops just short of the next pulse, so a step that falls
// on a pulse fires exactly with it even if the clock slows down or stops.
class ClockFollower {
public:
    void reset() { pulses_ = -1; }            // the next pulse is beat 0
    void setPpqn(int ppqn) { ppqn_ = ppqn > 0 ? ppqn : 1; }

    // Call once per sample, `pulse` being true on the sample of a rising clock edge.
    // `run` false stops playback (notes off) without losing the position.
    TransportState tick(bool pulse, bool run, double sampleRate);

private:
    int ppqn_ = 4;
    int64_t pulses_ = -1;            // -1: waiting for the first pulse after a reset
    double samplesSincePulse_ = 0;
    double periodSamples_ = 0;       // 0 until two pulses have been seen
    // Derived from the period and ppqn, recomputed on each pulse instead of each sample.
    int derivedPpqn_ = 0;
    double beatsPerSample_ = 0;
    double hold_ = 0;
    double tempo_ = 120.0;
};

constexpr int kDrumLanes = 8;

// Everything the module outputs for one sample, in a compact form (the expander message).
struct CvFrame {
    uint8_t channels[kNumTracks];
    uint8_t key[kNumTracks][kMaxVoices];
    uint8_t velocity[kNumTracks][kMaxVoices];   // 0..127
    uint8_t gateMask[kNumTracks];               // bit per voice, retrigger gaps applied
    uint8_t trigMask;                           // bit per track: a note started < 1 ms ago
    uint8_t laneGate[kNumTracks];               // bit per lane
    uint8_t laneTrig[kNumTracks];               // bit per lane
    uint8_t laneVelocity[kNumTracks][kDrumLanes];
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
    void setPoly(int track, bool poly)
    {
        frameDirty_ |= poly_[track] != poly;
        poly_[track] = poly;
    }
    void setRetrigSamples(int n) { retrigSamples_ = n > 0 ? n : 1; }

    // Call before rendering each sample.
    void beginSample();
    void emit(const NoteEvent& e) override;

    // The whole sample (gates, triggers, lanes): once per sample after rendering, always with the
    // same frame, which is only rewritten on change.
    void output(CvFrame& frame);

    // Per-voice access; gateOut consumes one sample of retrigger gap, so use it instead of output().
    int channels(int track) const { return poly_[track] ? channels_[track] : 1; }
    const Voice& voice(int track, int channel) const { return voices_[track][channel]; }
    bool gateOut(int track, int channel);

private:

    Voice voices_[kNumTracks][kMaxVoices];
    int channels_[kNumTracks] = {1, 1, 1, 1};
    bool poly_[kNumTracks] = {};
    int retrigSamples_ = 48;
    uint32_t counter_ = 0;
    bool changed_ = false;   // an event arrived since the last beginSample()

    struct Lane {
        bool gate = false;
        bool gateBefore = false;
        int retrig = 0;
        int trig = 0;
        uint8_t velocity = 0;
    };
    Lane lanes_[kNumTracks][kDrumLanes];
    int trig_[kNumTracks] = {};
    // output() fast path: the previous frame still holds when no event came in and no
    // retrigger/trigger countdown is running.
    bool frameDirty_ = true;
    bool countdowns_ = false;
};

} // namespace tetrad

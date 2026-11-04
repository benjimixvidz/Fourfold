#pragma once
// Sequencer engine. The caller feeds it the transport position in beats plus a frame count and
// receives sample-accurate note on/off events through a NoteSink. It never allocates and runs on
// the engine thread only.

#include "params.h"
#include "pattern.h"
#include "rng.h"

#include <cstdint>

namespace tetrad {

struct NoteEvent {
    uint32_t frame;   // offset inside the block being rendered
    bool on;
    uint8_t track;
    uint8_t key;      // 0..127
    float velocity;   // 0..1 (0 for note off)
    int8_t degree;    // scale degree of the step note (drum lanes use 0..7)
};

class NoteSink {
public:
    virtual ~NoteSink() = default;
    virtual void emit(const NoteEvent& e) = 0;   // called with non-decreasing frames
};

struct TransportState {
    bool playing = false;
    double beat = 0.0;     // position, in quarter notes, of the first rendered frame
    double tempo = 120.0;  // BPM
};

class Sequencer {
public:
    Sequencer();

    // Forgets sounding notes (without emitting offs) and the playback position.
    void reset(double sampleRate);

    // Regenerates every track whose generator settings differ from its pattern's key.
    void setSettings(const Settings& settings);
    const Settings& settings() const { return settings_; }

    void setPatterns(const PatternBank& bank);
    const PatternBank& patterns() const { return bank_; }
    // Bumped whenever patterns change, so the caller knows when to publish a copy.
    uint64_t patternVersion() const { return version_; }

    // Live transposition in semitones, applied to tracks that follow it.
    void setTranspose(int semitones) { transpose_ = semitones; }

    int playhead(int track) const { return playhead_[track]; }

    // Renders [firstFrame, firstFrame + numFrames) of the current block.
    void render(const TransportState& transport, uint32_t firstFrame, uint32_t numFrames, NoteSink& sink);

    // The fast path skips segments where nothing can happen (no step start, ratchet hit or note
    // off before the next known event). Only tests turn it off, to compare both paths.
    void setFastPath(bool enabled) { fastPath_ = enabled; }
    void allNotesOff(uint32_t frame, NoteSink& sink);

private:
    struct ActiveNote {
        uint64_t due;   // absolute frame of the note off
        uint8_t track, key;
        int8_t degree;
    };
    struct Trigger {
        uint32_t frame;
        int track;
        int64_t step;
        double beat;
        bool ratchetHit;   // a repeat inside a ratcheted step, not a new step
    };
    // Remaining hits of a ratcheted step; they may spill into later blocks.
    struct Ratchet {
        int remaining = 0;
        int index = 0;
        double nextBeat = 0.0;
        double interval = 0.0;
        double stepEnd = 0.0;
    };
    static constexpr int kMaxActive = 128;
    static constexpr int kMaxTriggers = 256;

    void addTrigger(int& count, const Trigger& t);
    uint32_t frameFor(double beat, double b0, double beatsPerFrame, uint32_t numFrames) const;
    void startStep(const Trigger& trig, double b0, double beatsPerFrame, uint32_t numFrames, uint32_t firstFrame,
                   int& numTriggers, int current, NoteSink& sink);
    void playNotes(int track, int index, double gateBeats, uint32_t localFrame, uint32_t firstFrame,
                   double beatsPerFrame, NoteSink& sink);
    int nextIndex(int track, int64_t k, int length);
    double stepStart(const TrackSettings& ts, int64_t k) const;
    void releaseDue(uint64_t limit, bool inclusive, uint32_t firstFrame, NoteSink& sink);
    void releaseAt(int index, uint32_t frame, NoteSink& sink);
    bool conditionPasses(int track, const Step& step, int64_t loop);
    void stopPlayback();
    void updateNextEvent(double b1, double epsilon);

    Settings settings_{};
    PatternBank bank_{};
    uint64_t version_ = 0;

    double sampleRate_ = 48000.0;
    uint64_t clock_ = 0;      // frames rendered since reset()
    uint64_t segStart_ = 0;   // clock_ at the start of the segment being rendered
    bool wasPlaying_ = false;
    int64_t lastStep_[kNumTracks];
    int playhead_[kNumTracks];
    int drunkPos_[kNumTracks];
    int64_t firstLoop_[kNumTracks];   // loop number of the first step since playback started
    bool lastCondition_[kNumTracks];  // result of the latest non-PRE trig condition
    Ratchet ratchet_[kNumTracks];
    int transpose_ = 0;

    // Fast path: valid while settings and transport continuity hold.
    bool fastPath_ = true;
    bool nextValid_ = false;
    double nextEventBeat_ = 0.0;   // earliest step start or ratchet hit still to come
    uint64_t nextDue_ = 0;         // earliest pending note off (absolute frame)
    double lastBeat_ = 0.0;        // b0 of the previous segment

    ActiveNote active_[kMaxActive];
    int numActive_ = 0;
    Trigger triggers_[kMaxTriggers];
    Rng rng_{0x7E7EAD};
};

} // namespace tetrad

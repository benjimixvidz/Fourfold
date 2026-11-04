#include "sequencer.h"

#include "scales.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace tetrad {

namespace {

int64_t positiveMod(int64_t a, int64_t m)
{
    const int64_t r = a % m;
    return r < 0 ? r + m : r;
}

int64_t floorDiv(int64_t a, int64_t m) { return (a - positiveMod(a, m)) / m; }

} // namespace

Sequencer::Sequencer()
{
    double values[kNumParams];
    defaultParamValues(values);
    setSettings(settingsFromValues(values));
    reset(48000.0);
}

void Sequencer::reset(double sampleRate)
{
    sampleRate_ = sampleRate > 0 ? sampleRate : 48000.0;
    clock_ = 0;
    segStart_ = 0;
    numActive_ = 0;
    for (int t = 0; t < kNumTracks; ++t) {
        playhead_[t] = 0;
        drunkPos_[t] = 0;
    }
    stopPlayback();
}

void Sequencer::stopPlayback()
{
    wasPlaying_ = false;
    nextValid_ = false;
    for (int t = 0; t < kNumTracks; ++t) {
        lastStep_[t] = INT64_MIN;
        ratchet_[t] = Ratchet{};
        firstLoop_[t] = INT64_MIN;
        lastCondition_[t] = false;
    }
}

void Sequencer::setSettings(const Settings& settings)
{
    settings_ = settings;
    nextValid_ = false;
    for (int t = 0; t < kNumTracks; ++t) {
        if (bank_.tracks[t].key != settings_.tracks[t].gen) {
            generatePattern(settings_.tracks[t].gen, bank_.tracks[t]);
            ++version_;
        }
    }
}

void Sequencer::setPatterns(const PatternBank& bank)
{
    bank_ = bank;
    nextValid_ = false;
    ++version_;
}

// Swing moves every odd step: 50 % = straight, above = later (shuffle), below = earlier.
double Sequencer::stepStart(const TrackSettings& ts, int64_t k) const
{
    const double swing = (std::clamp(ts.swing, 10, 90) - 50) / 50.0 * ts.stepBeats;
    return double(k) * ts.stepBeats + ((k & 1) ? swing : 0.0);
}

// Maps the absolute step counter (derived from the transport position) to a pattern index, so
// playback stays locked to the clock across loops and resets. Drunk is the only direction with
// memory: it wanders from wherever it was.
int Sequencer::nextIndex(int track, int64_t k, int length)
{
    const TrackSettings& ts = settings_.tracks[track];
    switch (ts.direction) {
    case kDirBackward:
        return length - 1 - int(positiveMod(k, length));
    case kDirPingPong: {
        if (length < 2)
            return 0;
        const int64_t period = 2 * int64_t(length) - 2;
        const int64_t m = positiveMod(k, period);
        return int(m < length ? m : period - m);
    }
    case kDirRandom:
        return int(mix64(uint64_t(k) ^ (uint64_t(uint32_t(ts.gen.seed)) << 32)) % uint64_t(length));
    case kDirDrunk: {
        // 50 % forward, 25 % repeat, 25 % back.
        const int r = rng_.percent();
        int pos = drunkPos_[track] + (r < 50 ? 1 : (r < 75 ? 0 : -1));
        pos = int(positiveMod(pos, length));
        drunkPos_[track] = pos;
        return pos;
    }
    default:
        return int(positiveMod(k, length));
    }
}

uint32_t Sequencer::frameFor(double beat, double b0, double beatsPerFrame, uint32_t numFrames) const
{
    // First frame at or after the position; positions a hair off an integer frame snap to it
    // instead of slipping by one.
    const double x = (beat - b0) / beatsPerFrame;
    const double nearest = std::round(x);
    const double offset = std::fabs(x - nearest) < 1e-3 ? nearest : std::ceil(x);
    return uint32_t(std::clamp(offset, 0.0, double(numFrames - 1)));
}

void Sequencer::addTrigger(int& count, const Trigger& t)
{
    if (count < kMaxTriggers)
        triggers_[count++] = t;
}

void Sequencer::render(const TransportState& tr, uint32_t firstFrame, uint32_t numFrames, NoteSink& sink)
{
    if (numFrames == 0)
        return;
    segStart_ = clock_;
    const uint64_t segEnd = clock_ + numFrames;

    const bool canPlay = tr.playing && std::isfinite(tr.beat) && std::isfinite(tr.tempo) && tr.tempo >= 1.0 &&
                         tr.tempo <= 2000.0 && std::fabs(tr.beat) < 1e12;
    if (!canPlay) {
        if (wasPlaying_) {
            allNotesOff(firstFrame, sink);
            stopPlayback();
        }
        clock_ = segEnd;
        return;
    }
    wasPlaying_ = true;

    const double beatsPerFrame = tr.tempo / 60.0 / sampleRate_;
    const double b0 = tr.beat;
    const double b1 = b0 + numFrames * beatsPerFrame;

    // Collect what starts inside [b0, b1). Both ends are shifted by a thousandth of a frame so
    // that a boundary sitting on the block edge, give or take rounding, consistently goes to the
    // next block's frame 0.
    const double epsilon = 1e-3 * beatsPerFrame;

    // Fast path: nothing starts and nothing ends in this segment.
    if (fastPath_ && nextValid_ && b0 >= lastBeat_ && b1 < nextEventBeat_ - epsilon && segEnd <= nextDue_) {
        lastBeat_ = b0;
        clock_ = segEnd;
        return;
    }

    int numTriggers = 0;
    for (int t = 0; t < kNumTracks; ++t) {
        const TrackSettings& ts = settings_.tracks[t];
        Ratchet& rt = ratchet_[t];
        // A muted track keeps running (playhead, Drunk walk, loop count) but stays silent.
        if (!ts.enabled)
            rt.remaining = 0;

        // Repeats of a ratcheted step started in an earlier block (dropped after a seek).
        if (rt.remaining > 0 && (b0 < rt.nextBeat - rt.interval - epsilon || b0 >= rt.stepEnd))
            rt.remaining = 0;
        double hit = rt.nextBeat;
        for (int r = 0; r < rt.remaining && hit < b1 - epsilon; ++r, hit += rt.interval)
            if (hit >= b0 - epsilon)
                addTrigger(numTriggers, {frameFor(hit, b0, beatsPerFrame, numFrames), t, lastStep_[t], hit, true});

        // Step starts. Swing can move an odd step by up to 0.8 step either way.
        const int64_t kHi = int64_t(std::floor(b1 / ts.stepBeats)) + 1;
        const int64_t kLo = std::max(int64_t(std::floor(b0 / ts.stepBeats)) - 1, kHi - kMaxTriggers);
        for (int64_t k = kLo; k <= kHi; ++k) {
            const double start = stepStart(ts, k);
            if (start < b0 - epsilon || start >= b1 - epsilon || k == lastStep_[t])
                continue;
            addTrigger(numTriggers, {frameFor(start, b0, beatsPerFrame, numFrames), t, k, start, false});
        }
    }

    // Stable insertion sort by frame (track order breaks ties).
    for (int i = 1; i < numTriggers; ++i) {
        const Trigger tmp = triggers_[i];
        int j = i;
        while (j > 0 && triggers_[j - 1].frame > tmp.frame) {
            triggers_[j] = triggers_[j - 1];
            --j;
        }
        triggers_[j] = tmp;
    }

    for (int i = 0; i < numTriggers; ++i) {
        const Trigger trig = triggers_[i];
        releaseDue(segStart_ + trig.frame, true, firstFrame, sink);
        startStep(trig, b0, beatsPerFrame, numFrames, firstFrame, numTriggers, i, sink);
    }
    releaseDue(segEnd, false, firstFrame, sink);
    clock_ = segEnd;
    lastBeat_ = b0;
    updateNextEvent(b1, epsilon);
}

void Sequencer::updateNextEvent(double b1, double epsilon)
{
    double next = HUGE_VAL;
    for (int t = 0; t < kNumTracks; ++t) {
        const TrackSettings& ts = settings_.tracks[t];
        const int64_t k0 = int64_t(std::floor(b1 / ts.stepBeats));
        for (int64_t k = k0 - 1; k <= k0 + 2; ++k) {
            const double start = stepStart(ts, k);
            if (start >= b1 - epsilon && start < next)
                next = start;
        }
        if (ratchet_[t].remaining > 0)
            next = std::min(next, ratchet_[t].nextBeat);
    }
    uint64_t due = UINT64_MAX;
    for (int i = 0; i < numActive_; ++i)
        due = std::min(due, active_[i].due);
    nextEventBeat_ = next;
    nextDue_ = due;
    nextValid_ = true;
}

void Sequencer::startStep(const Trigger& trig, double b0, double beatsPerFrame, uint32_t numFrames,
                          uint32_t firstFrame, int& numTriggers, int current, NoteSink& sink)
{
    const int track = trig.track;
    const TrackSettings& ts = settings_.tracks[track];
    Ratchet& rt = ratchet_[track];

    if (trig.ratchetHit) {
        if (rt.remaining <= 0)
            return;
        --rt.remaining;
        rt.nextBeat += rt.interval;
        const Step& step = bank_.tracks[track].steps[rt.index];
        playNotes(track, rt.index, rt.interval * ts.gate / 100.0 * step.gate / 100.0, trig.frame, firstFrame,
                  beatsPerFrame, sink);
        return;
    }

    lastStep_[track] = trig.step;
    rt.remaining = 0;   // a new step always ends the previous one's repeats
    const int length = std::clamp(ts.length, 1, kMaxSteps);
    // The loop window starts at `start` and wraps around the 128 steps.
    const int index = (std::clamp(ts.start, 0, kMaxSteps - 1) + nextIndex(track, trig.step, length)) % kMaxSteps;
    playhead_[track] = index;

    // Loop counter for trig conditions: one pass over the window (there and back for ping-pong).
    const int64_t period = (ts.direction == kDirPingPong && length >= 2) ? 2 * int64_t(length) - 2 : length;
    const int64_t loop = floorDiv(trig.step, period);
    if (firstLoop_[track] == INT64_MIN)
        firstLoop_[track] = loop;

    const Step& step = bank_.tracks[track].steps[index];
    if (!ts.enabled || !step.active || !conditionPasses(track, step, loop))
        return;
    const int probability = step.probability * std::clamp(ts.chance, 0, 100) / 100;
    if (probability < 100 && rng_.percent() >= probability)
        return;

    // The step lasts until the next step starts (swing makes odd and even steps differ).
    const double duration = stepStart(ts, trig.step + 1) - trig.beat;
    const int hits = std::clamp(int(step.ratchet), 1, kMaxRatchet);
    const double interval = duration / hits;
    playNotes(track, index, interval * ts.gate / 100.0 * step.gate / 100.0, trig.frame, firstFrame, beatsPerFrame,
              sink);
    if (hits == 1)
        return;

    rt.remaining = hits - 1;
    rt.index = index;
    rt.interval = interval;
    rt.nextBeat = trig.beat + interval;
    rt.stepEnd = trig.beat + duration;

    // Repeats falling inside this block join the pending triggers, keeping them sorted.
    const double b1 = b0 + numFrames * beatsPerFrame;
    const double epsilon = 1e-3 * beatsPerFrame;
    double hit = rt.nextBeat;
    for (int r = 0; r < rt.remaining && hit < b1 - epsilon && numTriggers < kMaxTriggers; ++r, hit += interval) {
        const Trigger t{frameFor(hit, b0, beatsPerFrame, numFrames), track, trig.step, hit, true};
        int j = numTriggers++;
        while (j > current + 1 && triggers_[j - 1].frame > t.frame) {
            triggers_[j] = triggers_[j - 1];
            --j;
        }
        triggers_[j] = t;
    }
}

bool Sequencer::conditionPasses(int track, const Step& step, int64_t loop)
{
    const int b = std::clamp(int(step.condB), 1, 8);
    const int a = std::clamp(int(step.condA), 1, b);
    bool result;
    switch (step.condition) {
    case kCondAB:
        result = positiveMod(loop, b) == a - 1;
        break;
    case kCondNotAB:
        result = positiveMod(loop, b) != a - 1;
        break;
    case kCondFirst:
        result = loop == firstLoop_[track];
        break;
    case kCondNotFirst:
        result = loop != firstLoop_[track];
        break;
    case kCondPre:
        return lastCondition_[track];
    case kCondNotPre:
        return !lastCondition_[track];
    default:
        return true;
    }
    lastCondition_[track] = result;
    return result;
}

void Sequencer::playNotes(int track, int index, double gateBeats, uint32_t localFrame, uint32_t firstFrame,
                          double beatsPerFrame, NoteSink& sink)
{
    const TrackSettings& ts = settings_.tracks[track];
    const Step& step = bank_.tracks[track].steps[index];
    const uint32_t frame = firstFrame + localFrame;
    const uint64_t now = segStart_ + localFrame;
    const bool mono = ts.mode == kModeMono;

    // Mono: a new hit always cuts the previous note of the same track.
    if (mono) {
        for (int i = numActive_ - 1; i >= 0; --i)
            if (active_[i].track == track)
                releaseAt(i, frame, sink);
    }

    const uint64_t gateFrames = std::max<uint64_t>(1, uint64_t(std::llround(gateBeats / beatsPerFrame)));
    const int velocity = std::clamp(step.velocity * ts.velocity / 100, 1, 127);
    const int voices = mono ? 1 : std::clamp(int(step.numNotes), 1, kMaxVoices);

    int played[kMaxVoices];
    int numPlayed = 0;
    for (int v = 0; v < voices; ++v) {
        const int key = trackKey(settings_, track, step.degree[v], ts.followTranspose ? transpose_ : 0);
        if (std::find(played, played + numPlayed, key) != played + numPlayed)
            continue;
        played[numPlayed++] = key;

        // Retriggering a key that is still sounding on this track: close it first.
        for (int i = numActive_ - 1; i >= 0; --i) {
            if (active_[i].track == track && active_[i].key == key) {
                releaseAt(i, frame, sink);
                break;
            }
        }
        if (numActive_ == kMaxActive) {
            int oldest = 0;
            for (int i = 1; i < numActive_; ++i)
                if (active_[i].due < active_[oldest].due)
                    oldest = i;
            releaseAt(oldest, frame, sink);
        }

        active_[numActive_++] = {now + gateFrames, uint8_t(track), uint8_t(key), step.degree[v]};
        sink.emit({frame, true, uint8_t(track), uint8_t(key), velocity / 127.0f, step.degree[v]});
    }
}

void Sequencer::releaseDue(uint64_t limit, bool inclusive, uint32_t firstFrame, NoteSink& sink)
{
    for (;;) {
        int best = -1;
        for (int i = 0; i < numActive_; ++i) {
            const uint64_t due = active_[i].due;
            if ((inclusive ? due <= limit : due < limit) && (best < 0 || due < active_[best].due))
                best = i;
        }
        if (best < 0)
            return;
        const uint64_t due = std::max(active_[best].due, segStart_);
        releaseAt(best, firstFrame + uint32_t(due - segStart_), sink);
    }
}

void Sequencer::releaseAt(int index, uint32_t frame, NoteSink& sink)
{
    const ActiveNote n = active_[index];
    active_[index] = active_[--numActive_];
    sink.emit({frame, false, n.track, n.key, 0.0f, n.degree});
}

void Sequencer::allNotesOff(uint32_t frame, NoteSink& sink)
{
    while (numActive_ > 0)
        releaseAt(numActive_ - 1, frame, sink);
}

} // namespace tetrad

#include "cv.h"

#include <algorithm>

namespace tetrad {

TransportState ClockFollower::tick(bool pulse, bool run, double sampleRate)
{
    samplesSincePulse_ += 1;
    if (pulse) {
        if (pulses_ >= 0)
            periodSamples_ = samplesSincePulse_;
        ++pulses_;
        samplesSincePulse_ = 0;
    }
    if ((pulse || derivedPpqn_ != ppqn_) && periodSamples_ > 0) {
        derivedPpqn_ = ppqn_;
        beatsPerSample_ = 1.0 / (periodSamples_ * ppqn_);
        tempo_ = 60.0 * sampleRate * beatsPerSample_;
        // Held more than one sample short of the next pulse: a step on that pulse must not
        // fire on the sample before it.
        hold_ = 1.0 / ppqn_ - 1.001 * beatsPerSample_;
    }

    // The clock counts as stopped after 4 periods (or 2 s before the first period is known).
    const double timeout = periodSamples_ > 0 ? 4 * periodSamples_ : 2 * sampleRate;
    TransportState tr;
    tr.playing = run && pulses_ >= 0 && samplesSincePulse_ < timeout;
    if (periodSamples_ > 0) {
        tr.tempo = tempo_;
        tr.beat = double(pulses_) / ppqn_ + std::min(samplesSincePulse_ * beatsPerSample_, hold_);
    } else {
        tr.tempo = 120.0;
        tr.beat = double(std::max<int64_t>(pulses_, 0)) / ppqn_;
    }
    return tr;
}

void CvVoices::clear()
{
    for (auto& track : voices_)
        for (auto& v : track)
            v = Voice{};
    for (int& c : channels_)
        c = 1;
    for (auto& track : lanes_)
        for (auto& l : track)
            l = Lane{};
    for (int& t : trig_)
        t = 0;
    changed_ = false;
    frameDirty_ = true;
    countdowns_ = false;
}

void CvVoices::beginSample()
{
    // Without events in the previous sample, gateBefore already equals gate.
    if (!changed_)
        return;
    changed_ = false;
    for (auto& track : voices_)
        for (auto& v : track)
            v.gateBefore = v.gate;
    for (auto& track : lanes_)
        for (auto& l : track)
            l.gateBefore = l.gate;
}

void CvVoices::emit(const NoteEvent& e)
{
    changed_ = true;
    frameDirty_ = true;

    // Drum lane of the note (scale degree 0..7), same retrigger rule as the voices.
    if (e.degree >= 0 && e.degree < kDrumLanes) {
        Lane& l = lanes_[e.track][e.degree];
        if (e.on) {
            if (l.gate || l.gateBefore)
                l.retrig = retrigSamples_;
            l.gate = true;
            l.trig = retrigSamples_;
            l.velocity = uint8_t(std::clamp(int(e.velocity * 127.f + 0.5f), 1, 127));
        } else {
            l.gate = false;
        }
    }
    if (e.on)
        trig_[e.track] = retrigSamples_;

    Voice* track = voices_[e.track];
    const int count = poly_[e.track] ? kMaxVoices : 1;
    if (!e.on) {
        for (int i = 0; i < count; ++i)
            if (track[i].gate && track[i].key == e.key) {
                track[i].gate = false;
                return;
            }
        return;
    }

    // A voice already on this key, else one that was silent, else any free one, else the oldest.
    Voice* v = nullptr;
    for (int i = 0; i < count && !v; ++i)
        if (track[i].gate && track[i].key == e.key)
            v = &track[i];
    for (int i = 0; i < count && !v; ++i)
        if (!track[i].gate && !track[i].gateBefore)
            v = &track[i];
    for (int i = 0; i < count && !v; ++i)
        if (!track[i].gate)
            v = &track[i];
    if (!v) {
        v = &track[0];
        for (int i = 1; i < count; ++i)
            if (track[i].age < v->age)
                v = &track[i];
    }

    // Re-striking the same key while it sounds gets a short low gate so envelopes retrigger;
    // a different key on a voice that was still high is a legato move (gate stays high).
    if ((v->gate || v->gateBefore) && v->key == e.key)
        v->retrig = retrigSamples_;
    v->key = e.key;
    v->velocity = e.velocity;
    v->gate = true;
    v->age = ++counter_;
    if (poly_[e.track])
        channels_[e.track] = std::max(channels_[e.track], int(v - track) + 1);
}

void CvVoices::output(CvFrame& f)
{
    if (!frameDirty_ && !countdowns_)
        return;
    frameDirty_ = false;
    bool counting = false;
    f.trigMask = 0;
    for (int t = 0; t < kNumTracks; ++t) {
        const int n = channels(t);
        f.channels[t] = uint8_t(n);
        uint8_t mask = 0;
        for (int c = 0; c < n; ++c) {
            counting |= voices_[t][c].retrig > 0;   // checked before gateOut counts it down
            if (gateOut(t, c))
                mask |= uint8_t(1u << c);
            f.key[t][c] = uint8_t(voices_[t][c].key);
            f.velocity[t][c] = uint8_t(std::clamp(int(voices_[t][c].velocity * 127.f + 0.5f), 0, 127));
        }
        f.gateMask[t] = mask;
        if (trig_[t] > 0) {
            f.trigMask |= uint8_t(1u << t);
            --trig_[t];
            counting = true;
        }
        uint8_t gates = 0, trigs = 0;
        for (int l = 0; l < kDrumLanes; ++l) {
            Lane& lane = lanes_[t][l];
            if (lane.gate && lane.retrig == 0)
                gates |= uint8_t(1u << l);
            if (lane.retrig > 0) {
                --lane.retrig;
                counting = true;
            }
            if (lane.trig > 0) {
                trigs |= uint8_t(1u << l);
                --lane.trig;
                counting = true;
            }
            f.laneVelocity[t][l] = lane.velocity;
        }
        f.laneGate[t] = gates;
        f.laneTrig[t] = trigs;
    }
    // One more pass after the last countdown ends, so the frame shows it finished.
    countdowns_ = counting;
    if (counting)
        frameDirty_ = true;
}

bool CvVoices::gateOut(int track, int channel)
{
    Voice& v = voices_[track][channel];
    const bool high = v.gate && v.retrig == 0;
    if (v.retrig > 0)
        --v.retrig;
    return high;
}

} // namespace tetrad

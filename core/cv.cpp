#include "cv.h"

#include <algorithm>
#include <cmath>

namespace tetrad {

void ClockFollower::setPpqn(int ppqn)
{
    ppqn = ppqn > 0 ? ppqn : 1;
    if (ppqn == ppqn_)
        return;
    if (pulses_ > 0) {
        baseBeat_ += double(pulses_) / ppqn_;
        pulses_ = 0;
    }
    ppqn_ = ppqn;
}

TransportState ClockFollower::tick(bool pulse, bool run, double sampleRate)
{
    // A new sample rate: what was counted in samples keeps its duration.
    if (rate_ > 0 && sampleRate != rate_) {
        const double k = sampleRate / rate_;
        periodSamples_ *= k;
        samplesSincePulse_ *= k;
        lastLongGap_ *= k;
        derivedPpqn_ = 0;
    }
    rate_ = sampleRate;
    if (age_ < 8)
        ++age_;
    samplesSincePulse_ += 1;
    if (pulse) {
        // A gap long enough for the transport to have stopped (more than four periods) is the
        // clock starting again after a stop, not a slower tempo. Taken as the period, it would give
        // the steps after the start a tempo far too slow (a gate as long as the stop, so the next
        // note comes without a new attack). The last period stays. Shorter gaps (swing, a missed
        // pulse, a slow-down) are measured as before. Before the first period is known, only the
        // gap after an edge on the very first samples can be a stop: a patch that opens over a
        // clock held high reads 0 V first (more than 2 s, the `timeout` below, means it was held).
        const double stopGap = periodSamples_ > 0 ? 4 * periodSamples_ : earlyEdge_ ? 2 * sampleRate : HUGE_VAL;
        bool restart = samplesSincePulse_ > stopGap;   // beyond: stopped (as 2.7.0)
        // Right after a restart, a second long gap like the first is a clock that slowed down by
        // more than four times: measured. Unlike it (a second stop), it is another restart.
        if (restart && trustNextGap_ && std::fabs(samplesSincePulse_ - lastLongGap_) <= 0.5 * lastLongGap_)
            restart = false;
        if (pulses_ >= 0 && !restart && !skipGap_)
            periodSamples_ = samplesSincePulse_;
        if (restart)
            lastLongGap_ = samplesSincePulse_;
        trustNextGap_ = restart;
        skipGap_ = false;
        earlyEdge_ = pulses_ < 0 && age_ <= 4;
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
        tr.beat = baseBeat_ + double(pulses_) / ppqn_ + std::min(samplesSincePulse_ * beatsPerSample_, hold_);
    } else {
        tr.tempo = 120.0;
        tr.beat = baseBeat_ + double(std::max<int64_t>(pulses_, 0)) / ppqn_;
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
    for (auto& track : rows_)
        for (auto& l : track)
            l = DrumRow{};
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
    for (auto& track : rows_)
        for (auto& l : track)
            l.gateBefore = l.gate;
}

void CvVoices::emit(const NoteEvent& e)
{
    changed_ = true;
    frameDirty_ = true;

    // Drum row of the note (scale degree 0..7, or the row of a Drum track), same retrigger rule as the voices.
    if (e.degree >= 0 && e.degree < kDrumRows) {
        DrumRow& l = rows_[e.track][e.degree];
        if (e.on) {
            if (l.gate || l.gateBefore)
                l.retrig = retrigSamples_;
            l.gate = true;
            l.trig = retrigSamples_;
            l.key = e.key;
            l.velocity = uint8_t(std::clamp(int(e.velocity * 127.f + 0.5f), 1, 127));
        } else if (e.key == l.key) {
            l.gate = false;
        }
    }
    if (e.on)
        trig_[e.track] = retrigSamples_;

    Voice* track = voices_[e.track];
    const int count = drum_[e.track] ? kDrumRows : poly_[e.track] ? kMaxVoices : 1;
    if (!e.on) {
        for (int i = 0; i < count; ++i)
            if (track[i].gate && track[i].key == e.key) {
                track[i].gate = false;
                return;
            }
        return;
    }

    // Drum: the row's own channel. Otherwise a voice already on this key, else one that was
    // silent, else any free one, else the oldest.
    Voice* v = drum_[e.track] && e.degree >= 0 && e.degree < kDrumRows ? &track[e.degree] : nullptr;
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
        for (int l = 0; l < kDrumRows; ++l) {
            DrumRow& row = rows_[t][l];
            if (row.gate && row.retrig == 0)
                gates |= uint8_t(1u << l);
            if (row.retrig > 0) {
                --row.retrig;
                counting = true;
            }
            if (row.trig > 0) {
                trigs |= uint8_t(1u << l);
                --row.trig;
                counting = true;
            }
            f.rowVelocity[t][l] = row.velocity;
        }
        f.rowGate[t] = gates;
        f.rowTrig[t] = trigs;
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

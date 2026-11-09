// Unit tests for the core (make test). No framework: each check prints and counts.

#include "cv.h"
#include "edit.h"
#include "grid.h"
#include "params.h"
#include "rng.h"
#include "scales.h"
#include "sequencer.h"
#include "state.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace tetrad;

namespace {

int failures = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

struct Event {
    uint64_t frame;   // absolute
    bool on;
    int track, key;
    float velocity;
};

// Records events with absolute frames and checks the per-block ordering contract.
class Recorder final : public NoteSink {
public:
    std::vector<Event> events;
    uint64_t blockStart = 0;
    uint32_t blockSize = 0;
    uint32_t lastFrame = 0;
    bool orderOk = true;

    void beginBlock(uint64_t start, uint32_t size)
    {
        blockStart = start;
        blockSize = size;
        lastFrame = 0;
    }

    void emit(const NoteEvent& e) override
    {
        if (e.frame < lastFrame || e.frame >= blockSize)
            orderOk = false;
        lastFrame = e.frame;
        events.push_back({blockStart + e.frame, e.on, e.track, e.key, e.velocity});
    }

    std::vector<Event> ons() const
    {
        std::vector<Event> r;
        for (const auto& e : events)
            if (e.on)
                r.push_back(e);
        return r;
    }
};

constexpr double kRate = 48000.0;

StepMask activeSteps(const PatternBank& bank, int track, const StepMask& within)
{
    StepMask m;
    for (int i = 0; i < kMaxSteps; ++i)
        if (within[size_t(i)] && bank.tracks[track].steps[i].active)
            m.set(size_t(i));
    return m;
}

// Plays `frames` frames at a constant tempo from `startBeat`, in blocks of `block` frames.
void play(Sequencer& seq, Recorder& rec, double tempo, double startBeat, uint64_t frames, uint32_t block,
          uint64_t startFrame = 0, double jitterBeats = 0.0)
{
    const double bpf = tempo / 60.0 / kRate;
    for (uint64_t pos = 0; pos < frames; pos += block) {
        const uint32_t n = uint32_t(std::min<uint64_t>(block, frames - pos));
        TransportState tr;
        tr.playing = true;
        tr.tempo = tempo;
        tr.beat = startBeat + double(pos) * bpf - (pos ? jitterBeats : 0.0);
        rec.beginBlock(startFrame + pos, n);
        seq.render(tr, 0, n, rec);
    }
}

void stop(Sequencer& seq, Recorder& rec, uint64_t atFrame, uint32_t block = 256)
{
    TransportState tr;
    tr.playing = false;
    rec.beginBlock(atFrame, block);
    seq.render(tr, 0, block, rec);
}

// Settings with only track 1 enabled; every step of track 1 active on degree `degreeOf(i)`.
Settings baseSettings()
{
    double values[kNumParams];
    defaultParamValues(values);
    values[trackParamIndex(0, kTrackLength)] = 8;
    values[trackParamIndex(0, kTrackGate)] = 50;
    return settingsFromValues(values);
}

void fillTrack(Sequencer& seq, int track, int numNotes = 1, bool degreeIsIndex = false)
{
    PatternBank bank = seq.patterns();
    for (int i = 0; i < kMaxSteps; ++i) {
        Step& s = bank.tracks[track].steps[i];
        s.active = true;
        s.numNotes = uint8_t(numNotes);
        const int base = degreeIsIndex ? i % 48 : 0;   // stays within int8 with 7 stacked voices
        for (int v = 0; v < kMaxVoices; ++v)
            s.degree[v] = int8_t(base + 2 * v);
        s.velocity = 100;
        s.gate = 100;
        s.probability = 100;
    }
    seq.setPatterns(bank);
}

void testGrid()
{
    std::printf("grid: 1/16 at 120 BPM lands every 6000 frames, gate 50 %%\n");
    Sequencer seq;
    seq.reset(kRate);
    seq.setSettings(baseSettings());
    fillTrack(seq, 0);
    Recorder rec;
    play(seq, rec, 120.0, 0.0, 48000, 512);
    const auto ons = rec.ons();
    CHECK(rec.orderOk);
    CHECK(ons.size() == 8);
    for (size_t i = 0; i < ons.size(); ++i) {
        CHECK(ons[i].frame == i * 6000);
        CHECK(ons[i].key == 60);
    }
    int offsAt3000 = 0;
    for (const auto& e : rec.events)
        if (!e.on && e.frame == 3000)
            ++offsAt3000;
    CHECK(offsAt3000 == 1);
}

void testTempoAndOffset()
{
    std::printf("tempo: 1/8 at 90 BPM starting mid-bar\n");
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.tracks[0].stepBeats = divisionBeats(3);   // 1/8
    seq.setSettings(s);
    fillTrack(seq, 0);
    Recorder rec;
    // Start at beat 0.3: the first step boundary is beat 0.5, i.e. 0.2 beat later.
    play(seq, rec, 90.0, 0.3, 96000, 333);
    const auto ons = rec.ons();
    CHECK(rec.orderOk);
    const double framesPerBeat = kRate * 60.0 / 90.0;   // 32000
    CHECK(!ons.empty());
    for (size_t i = 0; i < ons.size(); ++i) {
        const double expected = (0.2 + 0.5 * double(i)) * framesPerBeat;
        CHECK(std::fabs(double(ons[i].frame) - expected) <= 1.0);
    }
    CHECK(ons.size() == 6);   // beats 0.5 .. 3.0 within 3 beats of playback
}

void testSwing()
{
    std::printf("swing 75 %%: odd steps delayed by half a step\n");
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.tracks[0].swing = 75;
    seq.setSettings(s);
    fillTrack(seq, 0);
    Recorder rec;
    play(seq, rec, 120.0, 0.0, 24000, 256);
    const auto ons = rec.ons();
    CHECK(ons.size() == 4);
    const uint64_t expected[] = {0, 9000, 12000, 21000};
    for (size_t i = 0; i < ons.size() && i < 4; ++i)
        CHECK(ons[i].frame == expected[i]);
}

void testJitter()
{
    std::printf("jitter: overlapping block positions do not double-trigger\n");
    Sequencer seq;
    seq.reset(kRate);
    seq.setSettings(baseSettings());
    fillTrack(seq, 0);
    Recorder rec;
    play(seq, rec, 120.0, 0.0, 48000, 480, 0, 1e-5);
    CHECK(rec.ons().size() == 8);
    CHECK(rec.orderOk);
}

void testStop()
{
    std::printf("stop: sounding notes are released at the start of the stopped block\n");
    Sequencer seq;
    seq.reset(kRate);
    seq.setSettings(baseSettings());
    fillTrack(seq, 0);
    Recorder rec;
    play(seq, rec, 120.0, 0.0, 1024, 512);
    CHECK(rec.events.size() == 1 && rec.events[0].on);
    stop(seq, rec, 1024);
    CHECK(rec.events.size() == 2);
    if (rec.events.size() == 2) {
        CHECK(!rec.events[1].on);
        CHECK(rec.events[1].frame == 1024);
        CHECK(rec.events[1].key == rec.events[0].key);
    }
    // Stopped: nothing more comes out.
    stop(seq, rec, 1280);
    CHECK(rec.events.size() == 2);
}

void testMonoLegato()
{
    std::printf("mono, gate 200 %%: previous note is cut right before the next one\n");
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.tracks[0].gate = 200;
    seq.setSettings(s);
    fillTrack(seq, 0);
    Recorder rec;
    play(seq, rec, 120.0, 0.0, 12001, 1000);
    CHECK(rec.orderOk);
    // Expected: on@0, off@6000, on@6000, off@12000, on@12000
    CHECK(rec.events.size() == 5);
    if (rec.events.size() == 5) {
        CHECK(rec.events[1].frame == 6000 && !rec.events[1].on);
        CHECK(rec.events[2].frame == 6000 && rec.events[2].on);
    }
    int sounding = 0, maxSounding = 0;
    for (const auto& e : rec.events) {
        sounding += e.on ? 1 : -1;
        maxSounding = std::max(maxSounding, sounding);
    }
    CHECK(maxSounding == 1);
}

std::vector<int> keysFor(int direction, int length, int steps)
{
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.tracks[0].direction = direction;
    s.tracks[0].length = length;
    s.global.scale = kScaleChromatic;
    seq.setSettings(s);
    fillTrack(seq, 0, 1, true);
    Recorder rec;
    play(seq, rec, 120.0, 0.0, uint64_t(steps) * 6000, 600);
    std::vector<int> keys;
    for (const auto& e : rec.ons())
        keys.push_back(e.key - 60);
    return keys;
}

void testDirections()
{
    std::printf("directions: forward, backward, ping-pong, random stays in range\n");
    CHECK((keysFor(kDirForward, 3, 7) == std::vector<int>{0, 1, 2, 0, 1, 2, 0}));
    CHECK((keysFor(kDirBackward, 4, 6) == std::vector<int>{3, 2, 1, 0, 3, 2}));
    CHECK((keysFor(kDirPingPong, 4, 8) == std::vector<int>{0, 1, 2, 3, 2, 1, 0, 1}));
    const auto rnd = keysFor(kDirRandom, 5, 32);
    CHECK(rnd.size() == 32);
    for (int k : rnd)
        CHECK(k >= 0 && k < 5);
    // Random order is derived from the song position: replaying gives the same sequence.
    CHECK(rnd == keysFor(kDirRandom, 5, 32));
}

void testSwingEarly()
{
    std::printf("swing 25 %%: odd steps pulled half a step earlier, none lost at block edges\n");
    for (uint32_t block : {256u, 1000u, 2999u, 3001u}) {
        Sequencer seq;
        seq.reset(kRate);
        Settings s = baseSettings();
        s.tracks[0].swing = 25;
        seq.setSettings(s);
        fillTrack(seq, 0);
        Recorder rec;
        play(seq, rec, 120.0, 0.0, 24000, block);
        const auto ons = rec.ons();
        CHECK(rec.orderOk);
        CHECK(ons.size() == 4);
        const uint64_t expected[] = {0, 3000, 12000, 15000};
        for (size_t i = 0; i < ons.size() && i < 4; ++i)
            CHECK(ons[i].frame == expected[i]);
    }
}

void testRatchet()
{
    std::printf("ratchet: 3 hits per 1/16 step, spread evenly, across block edges\n");
    for (uint32_t block : {64u, 512u, 4096u}) {
        Sequencer seq;
        seq.reset(kRate);
        seq.setSettings(baseSettings());
        fillTrack(seq, 0);
        PatternBank bank = seq.patterns();
        for (auto& st : bank.tracks[0].steps)
            st.ratchet = 3;
        seq.setPatterns(bank);
        Recorder rec;
        play(seq, rec, 120.0, 0.0, 48000, block);
        const auto ons = rec.ons();
        CHECK(rec.orderOk);
        CHECK(ons.size() == 24);
        for (size_t i = 0; i < ons.size(); ++i)
            CHECK(ons[i].frame == i * 2000);
        // Gate 50 % of each 2000-frame hit: every note lasts 1000 frames.
        int sounding = 0, maxSounding = 0;
        for (const auto& e : rec.events) {
            sounding += e.on ? 1 : -1;
            maxSounding = std::max(maxSounding, sounding);
        }
        CHECK(maxSounding == 1);
    }
    // A seek in the middle of a ratcheted step drops its remaining hits.
    Sequencer seq;
    seq.reset(kRate);
    seq.setSettings(baseSettings());
    fillTrack(seq, 0);
    PatternBank bank = seq.patterns();
    for (auto& st : bank.tracks[0].steps)
        st.ratchet = 4;
    seq.setPatterns(bank);
    Recorder rec;
    play(seq, rec, 120.0, 0.0, 100, 100);           // first hit of step 0
    play(seq, rec, 120.0, 10.1, 100, 100, 100);     // jump far ahead, mid-step
    CHECK(rec.ons().size() == 1);
}

void testDrunk()
{
    std::printf("drunk: wanders by -1/0/+1 inside the loop\n");
    const auto keys = keysFor(kDirDrunk, 6, 200);
    CHECK(keys.size() == 200);
    int forward = 0, repeat = 0, back = 0;
    for (size_t i = 1; i < keys.size(); ++i) {
        CHECK(keys[i] >= 0 && keys[i] < 6);
        const int d = (keys[i] - keys[i - 1] + 6) % 6;
        CHECK(d == 0 || d == 1 || d == 5);
        forward += d == 1;
        repeat += d == 0;
        back += d == 5;
    }
    CHECK(forward > repeat && forward > back && repeat > 10 && back > 10);
}

void testLongPattern()
{
    std::printf("128 steps, 1/64 division, 7-voice poly\n");
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.tracks[0].length = 128;
    s.tracks[0].stepBeats = divisionBeats(10);   // 1/64
    s.tracks[0].mode = kModePoly;
    s.global.scale = kScaleChromatic;
    seq.setSettings(s);
    fillTrack(seq, 0, 7, true);
    Recorder rec;
    play(seq, rec, 120.0, 0.0, 1500 * 130, 512);   // 1/64 at 120 BPM = 1500 frames per step
    CHECK(rec.orderOk);
    const auto ons = rec.ons();
    CHECK(!ons.empty() && ons.size() % 7 == 0);
    // Step 129 wraps to step 0: the same root key as the first one.
    if (ons.size() >= 129 * 7) {
        CHECK(ons[0].frame == 0);
        CHECK(ons[128 * 7].frame == 128 * 1500);
        CHECK(ons[128 * 7].key == ons[0].key);
        CHECK(ons[127 * 7].frame == 127 * 1500);
    }
}

std::vector<int> playKeys(Sequencer& seq, int steps)
{
    Recorder rec;
    play(seq, rec, 120.0, 0.0, uint64_t(steps) * 6000, 700);
    std::vector<int> keys;
    for (const auto& e : rec.ons())
        keys.push_back(e.key - 60);
    return keys;
}

void testConditions()
{
    std::printf("trig conditions: A:B, Not A:B, 1st, PRE over 4 loops\n");
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.tracks[0].length = 4;
    s.global.scale = kScaleChromatic;
    seq.setSettings(s);
    fillTrack(seq, 0, 1, true);
    PatternBank bank = seq.patterns();
    Step* st = bank.tracks[0].steps;
    st[0].condition = kCondAB;    st[0].condA = 1; st[0].condB = 2;
    st[1].condition = kCondNotAB; st[1].condA = 1; st[1].condB = 2;
    st[2].condition = kCondFirst;
    st[3].condition = kCondPre;
    seq.setPatterns(bank);
    CHECK((playKeys(seq, 16) == std::vector<int>{0, 2, 3, 1, 0, 1}));

    // Restarting playback makes the current loop the "first" one again.
    TransportState stopped;
    Recorder rec;
    rec.beginBlock(0, 64);
    seq.render(stopped, 0, 64, rec);
    Recorder rec2;
    play(seq, rec2, 120.0, 8.0, 4 * 6000, 700);   // beat 8 = loop 8: step 0 plays (8 % 2 == 0)
    std::vector<int> keys;
    for (const auto& e : rec2.ons())
        keys.push_back(e.key - 60);
    CHECK((keys == std::vector<int>{0, 2, 3}));
}

void testStartWindow()
{
    std::printf("start: loop window moved inside the 128 steps, wrapping at the end\n");
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.global.scale = kScaleChromatic;
    s.tracks[0].start = 4;
    s.tracks[0].length = 3;
    seq.setSettings(s);
    fillTrack(seq, 0, 1, true);
    CHECK((playKeys(seq, 6) == std::vector<int>{4, 5, 6, 4, 5, 6}));

    Sequencer seq2;
    seq2.reset(kRate);
    s.tracks[0].start = 126;
    s.tracks[0].length = 4;
    seq2.setSettings(s);
    fillTrack(seq2, 0, 1, true);
    CHECK((playKeys(seq2, 4) == std::vector<int>{126 % 48, 127 % 48, 0, 1}));
}

void testGrid16x8()
{
    std::printf("pad grid: note toggles (mono/poly), lanes, pages, loop window\n");
    PatternBank bank;
    GridView view;
    view.track = 1;
    view.page = 2;          // steps 32..47
    view.lowestDegree = -2; // rows show -2 (bottom) .. +5 (top)
    Step& s = bank.tracks[1].steps[35];

    // Mono: press row for degree +3 -> active on +3; same pad again -> off; other row -> moves.
    const int rowPlus3 = kGridRows - 1 - (3 - view.lowestDegree);
    CHECK(pressPad(bank, view, kModeMono, 3, rowPlus3));
    CHECK(s.active && s.degree[0] == 3);
    CHECK(padInfo(bank, view, kModeMono, 7, 0, 64, 3, rowPlus3).lit);
    CHECK(pressPad(bank, view, kModeMono, 3, rowPlus3));
    CHECK(!s.active);
    CHECK(pressPad(bank, view, kModeMono, 3, 7));   // bottom row = -2
    CHECK(s.active && s.degree[0] == -2);

    // Poly: add up to 7 notes, an 8th replaces the oldest, pressing a lit one removes it.
    PatternBank pb;
    Step& p = pb.tracks[1].steps[35];
    for (int row = 7; row >= 0; --row)
        pressPad(pb, view, kModePoly, 3, row);
    CHECK(p.active && p.numNotes == kMaxVoices);
    CHECK(p.degree[0] == -1 && p.degree[6] == 5);   // -2 was pushed out by the 8th
    CHECK(padInfo(pb, view, kModePoly, 7, 0, 128, 3, 0).lit && !padInfo(pb, view, kModePoly, 7, 0, 128, 3, 7).lit);
    pressPad(pb, view, kModePoly, 3, 0);                 // remove +5
    CHECK(p.numNotes == 6 && !padInfo(pb, view, kModePoly, 7, 0, 128, 3, 0).lit);

    // Parameter lanes: top row = maximum, bottom row = minimum.
    view.lane = kLaneVelocity;
    pressPad(bank, view, kModeMono, 3, 0);
    CHECK(s.velocity == 127);
    view.lane = kLaneRatchet;
    pressPad(bank, view, kModeMono, 3, 4);
    CHECK(s.ratchet == 4);
    CHECK(padInfo(bank, view, kModeMono, 7, 0, 128, 3, 4).lit && !padInfo(bank, view, kModeMono, 7, 0, 128, 3, 3).lit);
    view.lane = kLaneCondition;
    pressPad(bank, view, kModeMono, 3, 4);   // 4th row from the bottom (row 4 from the top) = 1:4
    CHECK(s.condition == kCondAB && s.condA == 1 && s.condB == 4);
    view.lane = kLaneGate;
    pressPad(bank, view, kModeMono, 3, 0);
    CHECK(s.gate == 200);

    // Loop window: Start 30, Length 4 -> 0-based steps 29..32; only 32 is on page 2.
    view.lane = kLaneNote;
    CHECK(padInfo(bank, view, kModeMono, 7, 29, 4, 0, 0).inLoop);
    CHECK(!padInfo(bank, view, kModeMono, 7, 29, 4, 1, 0).inLoop);
    // Wrapping window: Start 127, Length 4 covers 126, 127, 0, 1.
    view.page = 0;
    CHECK(padInfo(bank, view, kModeMono, 7, 126, 4, 1, 0).inLoop && !padInfo(bank, view, kModeMono, 7, 126, 4, 2, 0).inLoop);
    // Root rows follow the scale size.
    view.lowestDegree = 0;
    CHECK(padInfo(bank, view, kModeMono, 7, 0, 16, 0, 7).rootRow && padInfo(bank, view, kModeMono, 7, 0, 16, 0, 0).rootRow);
    CHECK(!padInfo(bank, view, kModeMono, 5, 0, 16, 0, 0).rootRow);   // +7 is not a root in a 5-note scale
    char label[16];
    view.lane = kLaneCondition;
    CHECK(std::strcmp(rowLabel(view, kModeMono, 7, label, sizeof(label)), "--") == 0);
}

// Runs a sequencer from a pulse list (sample indices), sample by sample, like the Rack module.
struct ClockRun {
    std::vector<uint64_t> gateRises;   // samples where track 1's gate output goes high
    std::vector<int> keys;
    bool everHighAfterStop = false;
};

ClockRun runClock(Sequencer& seq, const std::vector<uint64_t>& pulses, uint64_t samples, int ppqn,
                  uint64_t resetAt = UINT64_MAX)
{
    ClockFollower clock;
    clock.setPpqn(ppqn);
    CvVoices voices;
    voices.setMode(0, seq.settings().tracks[0].mode);
    voices.setRetrigSamples(48);
    ClockRun r;
    bool before = false;
    size_t next = 0;
    for (uint64_t i = 0; i < samples; ++i) {
        if (i == resetAt)
            clock.reset();
        const bool pulse = next < pulses.size() && pulses[next] == i;
        if (pulse)
            ++next;
        const TransportState tr = clock.tick(pulse, true, kRate);
        voices.beginSample();
        seq.render(tr, 0, 1, voices);
        const bool high = voices.gateOut(0, 0);
        if (high && !before) {
            r.gateRises.push_back(i);
            r.keys.push_back(voices.voice(0, 0).key);
        }
        before = high;
    }
    return r;
}

// The clock stops then starts again: the gap of the stop is not a period, so the steps after the
// start keep the last tempo and each gets its own attack (a tempo taken from the stop would hold
// the first gate for as long as the stop, and the next note, on another key, would come legato).
void testClockRestart()
{
    std::printf("clock input: after a stop, the restart keeps the last tempo (every step gets its attack)\n");
    std::vector<uint64_t> pulses;
    for (uint64_t p = 0; p <= 42000; p += 6000)   // sixteenths at 120 BPM
        pulses.push_back(p);
    for (uint64_t p = 120000; p <= 162000; p += 6000)   // after a stop of 1.6 s
        pulses.push_back(p);
    Sequencer seq;
    seq.reset(kRate);
    seq.setSettings(baseSettings());
    fillTrack(seq, 0, 1, true);   // a different key on each step
    const ClockRun r = runClock(seq, pulses, 170000, 4);
    CHECK(r.gateRises.size() == pulses.size());
    for (size_t i = 0; i < std::min(r.gateRises.size(), pulses.size()); ++i)
        CHECK(r.gateRises[i] == pulses[i]);
    // A clock that slows down (three times longer) is measured at once, as in 2.7.0: only a gap
    // long enough to stop the transport is a restart.
    Sequencer slow;
    slow.reset(kRate);
    slow.setSettings(baseSettings());
    fillTrack(slow, 0, 1, true);
    const ClockRun s = runClock(slow, {0, 6000, 12000, 30000, 48000, 66000, 84000}, 90000, 4);
    CHECK((s.gateRises == std::vector<uint64_t>{0, 6000, 12000, 30000, 48000, 66000, 84000}));
}

// The tempo a clock follower reads, ticking pulses at the given samples up to sample `end`.
double followerTempo(ClockFollower& c, const std::vector<uint64_t>& pulses, uint64_t end, double rate)
{
    TransportState tr;
    size_t k = 0;
    for (uint64_t i = 0; i < end; ++i) {
        const bool pulse = k < pulses.size() && pulses[k] == i;
        k += pulse;
        tr = c.tick(pulse, true, rate);
    }
    return tr.tempo;
}

void testClockRestartCases()
{
    std::printf("clock input: two stops in a row, a clock slowing by six, a slow clock, a new sample rate\n");
    // Two stops with one pulse between them: the second stop is not a period either.
    std::vector<uint64_t> pulses;
    for (uint64_t p = 0; p <= 42000; p += 6000)
        pulses.push_back(p);
    pulses.push_back(120000);                            // after a stop of 1.6 s
    for (uint64_t p = 420000; p <= 462000; p += 6000)   // after a second stop of 6 s
        pulses.push_back(p);
    Sequencer seq;
    seq.reset(kRate);
    seq.setSettings(baseSettings());
    fillTrack(seq, 0, 1, true);
    const ClockRun r = runClock(seq, pulses, 470000, 4);
    CHECK(r.gateRises == pulses);
    // A clock that slows down six times: the first long gap is a restart, the second, like it, is
    // measured; every step keeps its attack.
    std::vector<uint64_t> slow;
    for (uint64_t p = 0; p <= 42000; p += 6000)
        slow.push_back(p);
    for (uint64_t p = 78000; p <= 258000; p += 36000)
        slow.push_back(p);
    Sequencer s6;
    s6.reset(kRate);
    s6.setSettings(baseSettings());
    fillTrack(s6, 0, 1, true);
    CHECK(runClock(s6, slow, 260000, 4).gateRises == slow);
    ClockFollower c6;
    c6.setPpqn(4);
    CHECK(std::fabs(followerTempo(c6, slow, 259000, kRate) - 20.0) < 0.01);   // 36000 samples a 1/16
    // A slow clock (3 s a pulse, 1 PPQN) that starts after the patch opened is measured at its
    // second pulse, as in 2.7.0: only an edge on the very first samples precedes a held clock.
    ClockFollower slowClock;
    slowClock.setPpqn(1);
    CHECK(std::fabs(followerTempo(slowClock, {4800, 148800}, 150000, kRate) - 20.0) < 0.01);
    // The sample rate changes from 44.1 to 192 kHz: the period keeps its duration (120 BPM), and
    // the next pulse, 0.5 s later, is not a stop.
    ClockFollower rate;
    rate.setPpqn(1);
    CHECK(std::fabs(followerTempo(rate, {0, 22050, 44100, 66150}, 70000, 44100.0) - 120.0) < 0.01);
    TransportState tr = rate.tick(false, true, 192000.0);
    CHECK(std::fabs(tr.tempo - 120.0) < 0.01);
    // 3850 samples at 44.1 kHz since the last pulse, then 1 + 79236 + 1 at 192 kHz: 0.5 s.
    for (int i = 0; i < 90000; ++i)
        tr = rate.tick(i == 79236, true, 192000.0);
    CHECK(tr.playing && std::fabs(tr.tempo - 120.0) < 0.05);
}

void testClockFollower()
{
    std::printf("clock input: 4 PPQN pulses drive 1/16 and 1/32 tracks, slowing, stopping, reset\n");
    std::vector<uint64_t> pulses;
    for (uint64_t p = 0; p < 48000; p += 6000)   // sixteenths at 120 BPM
        pulses.push_back(p);

    Sequencer seq;
    seq.reset(kRate);
    seq.setSettings(baseSettings());
    fillTrack(seq, 0);
    ClockRun r = runClock(seq, pulses, 48000, 4);
    CHECK(r.gateRises.size() == 8);
    for (size_t i = 0; i < r.gateRises.size(); ++i)
        CHECK(r.gateRises[i] == i * 6000);

    // 1/32 between the pulses: interpolated from the measured period.
    Sequencer seq32;
    seq32.reset(kRate);
    Settings s = baseSettings();
    s.tracks[0].stepBeats = divisionBeats(5);
    s.tracks[0].gate = 40;
    seq32.setSettings(s);
    fillTrack(seq32, 0);
    r = runClock(seq32, pulses, 48000, 4);
    CHECK(r.gateRises.size() == 15);   // the very first in-between step needs a measured period
    for (size_t i = 2; i < r.gateRises.size(); ++i)
        CHECK(r.gateRises[i] - r.gateRises[i - 1] == 3000);

    // The clock slows down: the step on the late pulse waits for it instead of running ahead.
    Sequencer slow;
    slow.reset(kRate);
    slow.setSettings(baseSettings());
    fillTrack(slow, 0);
    r = runClock(slow, {0, 6000, 18000, 30000}, 40000, 4);
    CHECK((r.gateRises == std::vector<uint64_t>{0, 6000, 18000, 30000}));

    // The clock stops: notes stop and nothing plays after the timeout.
    Sequencer stopped;
    stopped.reset(kRate);
    Settings legato = baseSettings();
    legato.tracks[0].gate = 200;
    stopped.setSettings(legato);
    fillTrack(stopped, 0);
    ClockFollower clock;
    CvVoices voices;
    std::vector<uint64_t> few = {0, 6000, 12000};
    size_t next = 0;
    bool highAtEnd = true;
    for (uint64_t i = 0; i < 12000 + 4 * 6000 + 10; ++i) {
        const bool pulse = next < few.size() && few[next] == i;
        if (pulse)
            ++next;
        voices.beginSample();
        stopped.render(clock.tick(pulse, true, kRate), 0, 1, voices);
        highAtEnd = voices.gateOut(0, 0);
    }
    CHECK(!highAtEnd);

    // Reset: the next pulse is step 1 again.
    Sequencer rs;
    rs.reset(kRate);
    Settings chroma = baseSettings();
    chroma.global.scale = kScaleChromatic;
    rs.setSettings(chroma);
    fillTrack(rs, 0, 1, true);
    r = runClock(rs, {0, 6000, 12000, 18000, 24000}, 30000, 4, 15000);
    CHECK((r.keys == std::vector<int>{60, 61, 62, 60, 61}));
}

void testPpqnChangeKeepsPosition()
{
    std::printf("clock input: a new PPQN keeps the position\n");
    ClockFollower clock;
    clock.setPpqn(24);
    TransportState tr;
    for (uint64_t i = 0; i < 240 * 1000; ++i)   // 240 pulses = 10 beats at 24 PPQN
        tr = clock.tick(i % 1000 == 0, true, kRate);
    const double before = tr.beat;
    clock.setPpqn(4);
    tr = clock.tick(false, true, kRate);
    CHECK(std::fabs(tr.beat - before) <= 0.25);   // only the time since the last pulse is reread
    for (int i = 0; i < 998; ++i)
        tr = clock.tick(false, true, kRate);
    tr = clock.tick(true, true, kRate);   // one more pulse = a quarter of a beat at 4 PPQN
    CHECK(std::fabs(tr.beat - (239.0 / 24.0 + 0.25)) < 1e-9);
}

void testDrunkRewind()
{
    std::printf("drunk: starts on the loop's first step, and again after a rewind\n");
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.global.scale = kScaleChromatic;
    s.tracks[0].direction = kDirDrunk;
    seq.setSettings(s);
    fillTrack(seq, 0, 1, true);
    Recorder rec;
    play(seq, rec, 120.0, 0.0, 48000, 64);   // 8 sixteenths
    CHECK(!rec.ons().empty() && rec.ons()[0].key == 60);
    seq.rewind();
    Recorder again;
    play(seq, again, 120.0, 0.0, 6000, 64, 48000);   // RESET: the transport back at beat 0
    CHECK(again.ons().size() == 1 && again.ons()[0].key == 60);
}

void testLaneKeepsNewerNote()
{
    std::printf("drum rows: an older note's off leaves the row to the newer note\n");
    CvVoices voices;
    voices.setMode(0, kModePoly);
    voices.setRetrigSamples(48);
    CvFrame f{};
    voices.beginSample();
    voices.emit({0, true, 0, 60, 0.8f, 0});    // degree 0 at C4
    voices.output(f);
    voices.beginSample();
    voices.emit({0, true, 0, 62, 0.8f, 0});    // degree 0 again, transposed: a new key
    voices.emit({0, false, 0, 60, 0.f, 0});    // the old note ends after it
    for (int i = 0; i < 100; ++i) {
        voices.output(f);
        voices.beginSample();
    }
    CHECK(f.rowGate[0] & 1u);
    voices.emit({0, false, 0, 62, 0.f, 0});
    voices.output(f);
    CHECK(!(f.rowGate[0] & 1u));
}

void testDrumMode()
{
    std::printf("drum mode: each row plays its own hits on its channel, scaled by its row, muted rows silent\n");
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.tracks[0].mode = kModeDrum;
    s.tracks[0].gate = 50;
    seq.setSettings(s);
    PatternBank bank = seq.patterns();
    for (auto& st : bank.tracks[0].steps)
        st = Step{};
    bank.tracks[0].steps[0].hits = 0b101;          // rows 1 and 3
    bank.tracks[0].steps[0].drum[0].velocity = 100;
    bank.tracks[0].steps[0].drum[2].velocity = 120;
    bank.tracks[0].steps[1].hits = 0b10;           // row 2, two ratchet hits
    bank.tracks[0].steps[1].drum[1].ratchet = 2;
    bank.tracks[0].steps[2].hits = 0b100;          // row 3 again, but the row is muted
    bank.tracks[0].rows[0].level = 50;
    bank.tracks[0].steps[3].hits = 0b1;
    bank.tracks[0].steps[3].drum[0].velocity = 100;
    seq.setPatterns(bank);
    bank.tracks[0].rows[2].mute = true;   // applied from step 2 on
    Recorder rec;
    play(seq, rec, 120.0, 0.0, 12000, 64);   // steps 0 and 1
    seq.setPatterns(bank);
    play(seq, rec, 120.0, 12000 * 120.0 / 60.0 / kRate, 12000, 64, 12000);   // steps 2 and 3
    const auto ons = rec.ons();
    CHECK(ons.size() == 5);
    if (ons.size() == 5) {
        CHECK(ons[0].key == 60 && ons[1].key == 62 && ons[0].frame == 0 && ons[1].frame == 0);
        CHECK(std::lround(ons[0].velocity * 127) == 50 && std::lround(ons[1].velocity * 127) == 120);
        CHECK(ons[2].key == 61 && ons[3].key == 61 && ons[2].frame == 6000 && ons[3].frame == 9000);
        CHECK(ons[4].key == 60 && ons[4].frame == 18000);   // step 2's muted row 3 is silent
    }

    CvVoices voices;
    voices.setMode(0, kModeDrum);
    CHECK(voices.channels(0) == kDrumRows);
    voices.beginSample();
    voices.emit({0, true, 0, 62, 1.f, 2});
    CvFrame f{};
    voices.output(f);
    CHECK(f.channels[0] == kDrumRows && f.gateMask[0] == 0b100 && f.key[0][2] == 62 && (f.rowGate[0] & 0b100));
}

void testStateVersion3()
{
    std::printf("state: version 3 documents (no drum part) still load\n");
    PatternBank bank;
    generatePattern({7, 60, 8, 1, 0, 0}, bank.tracks[0]);
    std::string text = serializePatterns(bank);
    std::string v3;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line))
        if (line.rfind("row ", 0) != 0 && line.rfind("drum ", 0) != 0)
            v3 += line + "\n";
    v3.replace(v3.find(" 4\n"), 3, " 3\n");
    PatternBank loaded;
    CHECK(deserializePatterns(v3, loaded));
    CHECK(loaded.tracks[0].steps[5].active == bank.tracks[0].steps[5].active);
    CHECK(loaded.tracks[0].steps[0].hits == 0 && loaded.tracks[0].rows[3].level == 100);
}

void testDrumGrid()
{
    std::printf("drum grid: the Note lane toggles hits, value lanes and edits act on the focused row\n");
    PatternBank bank;
    GridView view;
    view.lane = kLaneNote;
    CHECK(pressPad(bank, view, kModeDrum, 2, 7));   // bottom grid row = row 1
    CHECK(pressPad(bank, view, kModeDrum, 2, 5));   // row 3
    CHECK(bank.tracks[0].steps[2].hits == 0b101);
    CHECK(padInfo(bank, view, kModeDrum, 7, 0, 16, 2, 5).lit && !padInfo(bank, view, kModeDrum, 7, 0, 16, 2, 6).lit);
    view.focusRow = 2;
    view.lane = kLaneVelocity;
    pressPad(bank, view, kModeDrum, 2, 4);          // 4th level from the bottom = 64
    CHECK(bank.tracks[0].steps[2].drum[2].velocity == 64 && bank.tracks[0].steps[2].drum[0].velocity == 100);
    char label[8];
    view.lane = kLaneNote;
    CHECK(std::strcmp(rowLabel(view, kModeDrum, 0, label, sizeof(label)), "R8") == 0);

    StepMask sel;
    sel.set(2);
    CHECK(adjustSteps(bank, 0, sel, kStepRatchet, 2, 2));
    CHECK(bank.tracks[0].steps[2].drum[2].ratchet == 3 && bank.tracks[0].steps[2].ratchet == 1);
    euclidRow(bank, 0, 0, 16, 4, 0, 1);
    int count = 0;
    for (int i = 0; i < 16; ++i)
        count += (bank.tracks[0].steps[i].hits >> 1) & 1;
    CHECK(count == 4 && (bank.tracks[0].steps[2].hits & 0b101) == 0b101);
}

void testCvVoices()
{
    std::printf("cv voices: retrigger on the same key, legato on a new key, poly channels\n");
    CvVoices v;
    v.setRetrigSamples(3);
    auto on = [&](int track, int key) { v.emit({0, true, uint8_t(track), uint8_t(key), 0.5f, 0}); };
    auto off = [&](int track, int key) { v.emit({0, false, uint8_t(track), uint8_t(key), 0.f, 0}); };

    v.beginSample();
    on(0, 60);
    CHECK(v.gateOut(0, 0));
    v.beginSample();
    off(0, 60);
    on(0, 60);   // same key, same sample: 3 samples low, then high
    CHECK(!v.gateOut(0, 0));
    v.beginSample();
    CHECK(!v.gateOut(0, 0));
    v.beginSample();
    CHECK(!v.gateOut(0, 0));
    v.beginSample();
    CHECK(v.gateOut(0, 0));
    v.beginSample();
    off(0, 60);
    on(0, 64);   // new key: legato, the gate never drops
    CHECK(v.gateOut(0, 0) && v.voice(0, 0).key == 64);
    CHECK(v.channels(0) == 1);

    v.setMode(1, kModePoly);
    v.beginSample();
    on(1, 60);
    on(1, 63);
    on(1, 67);
    CHECK(v.channels(1) == 3);
    CHECK(v.voice(1, 0).key == 60 && v.voice(1, 1).key == 63 && v.voice(1, 2).key == 67);
    v.beginSample();
    off(1, 63);
    v.beginSample();
    on(1, 70);   // goes to the voice that was free for a whole sample
    CHECK(v.voice(1, 1).key == 70 && v.channels(1) == 3);
}

void testFastPathEquivalence()
{
    std::printf("fast path: identical events with and without it (random settings, seeks, blocks)\n");
    for (uint32_t seed : {11u, 22u, 33u}) {
        Sequencer fast, slow;
        slow.setFastPath(false);
        fast.reset(44100.0);
        slow.reset(44100.0);
        Recorder a, b;
        Rng rng(seed);
        uint64_t frame = 0;
        double beat = 0.0, tempo = 133.0;
        bool playing = true;
        for (int i = 0; i < 20000; ++i) {
            if (rng.below(300) == 0) {
                double values[kNumParams];
                const ParamDesc* table = paramTable();
                for (int p = 0; p < kNumParams; ++p)
                    values[p] = table[p].min + double(rng.below(uint32_t(table[p].max - table[p].min + 1)));
                fast.setSettings(settingsFromValues(values));
                slow.setSettings(settingsFromValues(values));
            }
            if (rng.below(500) == 0)
                beat = double(rng.below(4000)) / 7.0;
            if (rng.below(400) == 0)
                tempo = 40.0 + double(rng.below(300));
            if (rng.below(700) == 0)
                playing = !playing;
            const uint32_t n = rng.below(4) == 0 ? 1 + rng.below(512) : 1;   // mostly 1-sample, like Rack
            TransportState tr{playing, beat, tempo};
            a.beginBlock(frame, n);
            b.beginBlock(frame, n);
            fast.render(tr, 0, n, a);
            slow.render(tr, 0, n, b);
            frame += n;
            beat += n * tempo / 60.0 / 44100.0;
        }
        bool same = a.events.size() == b.events.size();
        for (size_t i = 0; same && i < a.events.size(); ++i)
            same = a.events[i].frame == b.events[i].frame && a.events[i].on == b.events[i].on &&
                   a.events[i].key == b.events[i].key && a.events[i].track == b.events[i].track;
        CHECK(same);
        CHECK(a.events.size() > 200);
    }
}

void testStepFields()
{
    std::printf("step fields: clamped edits, gate tie, A:B rules, reset, set\n");
    PatternBank bank;
    StepMask one;
    one.set(9);
    Step& step = bank.tracks[3].steps[9];
    step.gate = 98;
    CHECK(adjustSteps(bank, 3, one, kStepGate, 5) && step.gate == 100);
    CHECK(adjustSteps(bank, 3, one, kStepGate, 1) && step.gate == kGateTie);
    CHECK(!adjustSteps(bank, 3, one, kStepGate, 3));                    // stays tied
    CHECK(adjustSteps(bank, 3, one, kStepGate, -5) && step.gate == 100);
    CHECK(adjustSteps(bank, 3, one, kStepCondition, 1) && step.condition == kCondAB && step.condB == 2);
    CHECK(adjustSteps(bank, 3, one, kStepCondA, 5) && step.condA == 2);    // A stays <= B
    CHECK(adjustSteps(bank, 3, one, kStepCondB, -1) && step.condB == 1 && step.condA == 1);
    CHECK(!adjustSteps(bank, 3, one, kStepRatchet, -1));                   // already at 1
    CHECK(adjustSteps(bank, 3, one, kStepNotes, 10) && step.numNotes == kMaxVoices);
    CHECK(setSteps(bank, 3, one, kStepVelocity, 300) && step.velocity == 127);
    CHECK(setSteps(bank, 3, one, kStepGate, 150) && step.gate == kGateTie);
    CHECK(resetSteps(bank, 3, one, kStepVelocity) && step.velocity == Step{}.velocity);
    setSteps(bank, 3, one, kStepCondition, kCondNotAB);
    CHECK(resetSteps(bank, 3, one, kStepCondB) && step.condition == kCondAlways && step.condA == 1 && step.condB == 1);
    CHECK(!resetSteps(bank, 3, one, kStepCondB));
    CHECK(std::strcmp(conditionName(kCondNotPre), "NOT PRE") == 0 && std::strcmp(conditionName(99), "?") == 0);
}

void testDrumLanes()
{
    std::printf("drum rows: a poly track's rows 0..7 get their own gate, trigger and velocity\n");
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.tracks[0].mode = kModePoly;
    seq.setSettings(s);
    PatternBank bank = seq.patterns();
    for (auto& st : bank.tracks[0].steps)
        st.active = false;
    // Step 0: kick (lane 0) + hat (lane 2); step 1: snare (lane 1); step 2: kick again.
    auto set = [&](int i, std::initializer_list<int> degrees, int vel) {
        Step& st = bank.tracks[0].steps[i];
        st.active = true;
        st.numNotes = uint8_t(degrees.size());
        int v = 0;
        for (int d : degrees)
            st.degree[v++] = int8_t(d);
        st.velocity = uint8_t(vel);
    };
    set(0, {0, 2}, 120);
    set(1, {1}, 90);
    set(2, {0}, 60);
    seq.setPatterns(bank);

    ClockFollower clock;
    clock.setPpqn(4);
    CvVoices voices;
    voices.setMode(0, kModePoly);
    voices.setRetrigSamples(48);
    CvFrame f{};
    std::vector<int> rowRises[kDrumRows];
    int trigSamples = 0;
    uint8_t before = 0;
    for (uint64_t i = 0; i < 18000; ++i) {
        const bool pulse = i % 6000 == 0;
        voices.beginSample();
        seq.render(clock.tick(pulse, true, kRate), 0, 1, voices);
        voices.output(f);
        for (int l = 0; l < kDrumRows; ++l)
            if ((f.rowGate[0] & (1u << l)) && !(before & (1u << l)))
                rowRises[l].push_back(int(i));
        before = f.rowGate[0];
        trigSamples += (f.rowTrig[0] & 1u) ? 1 : 0;
        if (i == 10)
            CHECK(f.rowVelocity[0][0] == 120 && f.rowVelocity[0][2] == 120 && (f.trigMask & 1u));
        if (i == 6010)
            CHECK(f.rowVelocity[0][1] == 90 && !(f.rowGate[0] & 1u));
    }
    CHECK((rowRises[0] == std::vector<int>{0, 12000}));
    CHECK((rowRises[1] == std::vector<int>{6000}));
    CHECK((rowRises[2] == std::vector<int>{0}));
    CHECK(rowRises[3].empty());
    CHECK(trigSamples == 2 * 48);   // two kick triggers of 1 ms
    CHECK(f.rowVelocity[0][0] == 60);
}

void testCvFrameFastPath()
{
    std::printf("cv frame: skipping unchanged samples gives the same frames as recomputing them\n");
    // Reference: a CvVoices whose frame is recomputed from a fresh object every sample is not
    // possible (state), so compare against gateOut-based outputs on a twin instance.
    Sequencer seqA, seqB;
    Settings s = baseSettings();
    s.tracks[0].mode = kModePoly;
    s.tracks[1].enabled = true;
    s.tracks[1].gate = 200;   // legato / retrigger cases
    s.tracks[0].gen.ratchet = 60;
    seqA.setSettings(s);
    seqB.setSettings(s);
    seqA.reset(kRate);
    seqB.reset(kRate);
    ClockFollower ca, cb;
    CvVoices a, b;
    a.setMode(0, kModePoly);
    b.setMode(0, kModePoly);
    a.setRetrigSamples(30);
    b.setRetrigSamples(30);
    CvFrame fa{};
    bool same = true;
    for (uint64_t i = 0; i < 96000 && same; ++i) {
        const bool pulse = i % 6000 == 0;
        a.beginSample();
        b.beginSample();
        seqA.render(ca.tick(pulse, true, kRate), 0, 1, a);
        seqB.render(cb.tick(pulse, true, kRate), 0, 1, b);
        a.output(fa);
        for (int t = 0; t < kNumTracks && same; ++t)
            for (int c = 0; c < b.channels(t); ++c) {
                const bool g = b.gateOut(t, c);
                same = same && (((fa.gateMask[t] >> c) & 1u) != 0) == g && fa.key[t][c] == b.voice(t, c).key;
            }
    }
    CHECK(same);
}

void testEditOps()
{
    std::printf("edit ops: copy/paste with gaps, clear, shift, euclid, relative edit, transpose\n");
    PatternBank bank;
    for (int i = 0; i < 8; ++i) {
        Step& s = bank.tracks[0].steps[i];
        s.active = i % 2 == 0;
        s.degree[0] = int8_t(i);
        s.velocity = uint8_t(10 * (i + 1));
    }
    Clipboard clip;
    StepMask sel;
    sel.set(2);
    sel.set(5);
    copySteps(bank, 0, sel, clip);
    CHECK(clip.count == 4 && clip.used[0] && clip.used[3] && !clip.used[1]);
    const StepMask w = pasteSteps(bank, 1, 126, clip);   // wraps: 126 and 1
    CHECK(w[126] && w[1] && w.count() == 2);
    CHECK(bank.tracks[1].steps[126].degree[0] == 2 && bank.tracks[1].steps[1].degree[0] == 5);

    shiftSteps(bank, 0, windowMask(0, 8), 1);   // rotate right by one inside 0..7
    CHECK(bank.tracks[0].steps[1].degree[0] == 0 && bank.tracks[0].steps[0].degree[0] == 7);

    clearSteps(bank, 0, rangeMask(3, 1));
    CHECK(!bank.tracks[0].steps[1].active && bank.tracks[0].steps[2].velocity == 75);

    PatternBank e;
    euclid(e, 2, 120, 16, 4, 0, 3);   // 4 on the floor across the wrap 120..135 -> 120..7
    StepMask hits = activeSteps(e, 2, windowMask(120, 16));
    CHECK(hits.count() == 4 && hits[120] && hits[124] && hits[0] && hits[4]);
    CHECK(e.tracks[2].steps[124].degree[0] == 3);
    euclid(e, 2, 0, 8, 3, 0, 0);   // E(3,8) = x..x..x.
    hits = activeSteps(e, 2, windowMask(0, 8));
    CHECK(hits.count() == 3 && hits[0] && hits[3] && hits[6]);

    PatternBank r;
    StepMask all = windowMask(0, 4);
    CHECK(adjustSteps(r, 0, all, kStepVelocity, 10));
    CHECK(r.tracks[0].steps[3].velocity == 85);
    CHECK(transposeSteps(r, 0, rangeMask(0, 1), -2) && r.tracks[0].steps[0].degree[0] == -2 &&
          r.tracks[0].steps[2].degree[0] == 0);

    // Expand / condense / reverse on a window that wraps (126..129 -> 126, 127, 0, 1).
    PatternBank x;
    for (int i = 0; i < 4; ++i) {
        Step& s = x.tracks[3].steps[(126 + i) % 128];
        s.active = true;
        s.degree[0] = int8_t(i + 1);
    }
    CHECK(expandSteps(x, 3, 126, 4));
    CHECK(x.tracks[3].steps[126].degree[0] == 1 && !x.tracks[3].steps[127].active &&
          x.tracks[3].steps[0].degree[0] == 2 && x.tracks[3].steps[4].degree[0] == 4 && !x.tracks[3].steps[5].active);
    condenseSteps(x, 3, 126, 8);
    CHECK(x.tracks[3].steps[126].degree[0] == 1 && x.tracks[3].steps[127].degree[0] == 2 &&
          x.tracks[3].steps[1].degree[0] == 4 && !x.tracks[3].steps[2].active);
    reverseSteps(x, 3, 126, 4);
    CHECK(x.tracks[3].steps[126].degree[0] == 4 && x.tracks[3].steps[1].degree[0] == 1);
    CHECK(!expandSteps(x, 3, 0, 65));
}

void testPolyAndScale()
{
    std::printf("poly: three stacked thirds in C minor\n");
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.tracks[0].mode = kModePoly;
    seq.setSettings(s);
    fillTrack(seq, 0, 3);
    Recorder rec;
    play(seq, rec, 120.0, 0.0, 1000, 500);
    const auto ons = rec.ons();
    CHECK(ons.size() == 3);
    if (ons.size() == 3) {
        CHECK(ons[0].key == 60);
        CHECK(ons[1].key == 63);
        CHECK(ons[2].key == 67);
    }
    CHECK(degreeToSemitones(kScaleMajor, -1) == -1);
    CHECK(degreeToSemitones(kScaleMajor, 7) == 12);
    CHECK(degreeToSemitones(kScalePentaMinor, 6) == 15);
}

void testTranspose()
{
    std::printf("transpose: +2 semitones on following tracks only\n");
    Sequencer seq;
    seq.reset(kRate);
    Settings s = baseSettings();
    s.tracks[1] = s.tracks[0];
    s.tracks[1].enabled = true;
    s.tracks[1].followTranspose = false;
    seq.setSettings(s);
    fillTrack(seq, 0);
    fillTrack(seq, 1);
    seq.setTranspose(2);
    Recorder rec;
    play(seq, rec, 120.0, 0.0, 1000, 500);
    const auto ons = rec.ons();
    CHECK(ons.size() == 2);
    for (const auto& e : ons)
        CHECK(e.key == (e.track == 0 ? 62 : 60));
    CHECK(trackKey(s, 0, 0, 200) <= 127 && trackKey(s, 0, 0, -200) >= 0);
}

void testGenerator()
{
    std::printf("generator: deterministic, density respected, regenerates only on key change\n");
    TrackPattern a, b;
    GenKey key{42, 50, 7, 2};
    generatePattern(key, a);
    generatePattern(key, b);
    CHECK(std::memcmp(&a, &b, sizeof(a)) == 0);
    GenKey none{42, 0, 7, 2};
    generatePattern(none, a);
    int active = 0;
    for (const auto& s : a.steps)
        active += s.active;
    CHECK(active == 0);
    GenKey all{42, 100, 7, 2};
    generatePattern(all, a);
    active = 0;
    for (const auto& s : a.steps) {
        active += s.active;
        CHECK(s.degree[0] >= 0 && s.degree[0] < 7);
        CHECK(s.numNotes == 2);
    }
    CHECK(active == kMaxSteps);

    // Ratchets and Variation use their own random streams: the notes stay the same.
    TrackPattern plain, spiced;
    generatePattern({9, 80, 7, 1, 0, 0}, plain);
    generatePattern({9, 80, 7, 1, 50, 100}, spiced);
    int conditioned = 0, ratcheted = 0;
    for (int i = 0; i < kMaxSteps; ++i) {
        CHECK(plain.steps[i].active == spiced.steps[i].active);
        CHECK(plain.steps[i].degree[0] == spiced.steps[i].degree[0]);
        CHECK(plain.steps[i].condition == kCondAlways && plain.steps[i].ratchet == 1);
        conditioned += spiced.steps[i].condition != kCondAlways;
        ratcheted += spiced.steps[i].ratchet > 1;
        if (!spiced.steps[i].active)
            CHECK(spiced.steps[i].condition == kCondAlways && spiced.steps[i].ratchet == 1);
    }
    CHECK(conditioned > 50 && ratcheted > 20);

    Sequencer seq;
    const uint64_t v0 = seq.patternVersion();
    Settings s = seq.settings();
    s.tracks[0].gate = 33;   // not a generator setting
    seq.setSettings(s);
    CHECK(seq.patternVersion() == v0);
    s.tracks[0].gen.seed = 77;
    seq.setSettings(s);
    CHECK(seq.patternVersion() == v0 + 1);
    CHECK(seq.patterns().tracks[0].key.seed == 77);
}

void testState()
{
    std::printf("state: exact round trip, older documents load, invalid data rejected\n");
    PatternBank bank;
    for (int t = 0; t < kNumTracks; ++t)
        generatePattern({t * 3, 60, 9, 1 + t}, bank.tracks[t]);
    bank.tracks[3].steps[5].degree[1] = -7;
    bank.tracks[2].steps[7].condition = kCondNotAB;
    bank.tracks[2].steps[7].condA = 3;
    bank.tracks[2].steps[7].condB = 4;
    bank.tracks[1].steps[100].ratchet = 6;

    const std::string text = serializePatterns(bank);
    PatternBank bank2;
    CHECK(deserializePatterns(text, bank2));
    CHECK(std::memcmp(&bank, &bank2, sizeof(PatternBank)) == 0);
    CHECK(serializePatterns(bank2) == text);

    // Without the trailing newline, with CRLF line ends, and with the parameter lines that
    // earlier versions wrote.
    CHECK(deserializePatterns(text.substr(0, text.size() - 1), bank2));
    std::string crlf;
    for (char c : text) {
        if (c == '\n')
            crlf += '\r';
        crlf += c;
    }
    CHECK(deserializePatterns(crlf, bank2));
    std::string withParams = text;
    withParams.insert(withParams.find('\n') + 1, "param 102 4008000000000000\n");
    CHECK(deserializePatterns(withParams, bank2) && std::memcmp(&bank, &bank2, sizeof(PatternBank)) == 0);

    PatternBank probe = bank;
    CHECK(!deserializePatterns("", probe));
    CHECK(!deserializePatterns(text.substr(0, text.size() / 2), probe));
    CHECK(!deserializePatterns("garbage\n", probe));
    CHECK(!deserializePatterns("tetrad-seq-state 1\nend\n", probe));
    CHECK(!deserializePatterns("tetrad-seq-state 3\nstep 0 1 1\nend\n", probe));
    Rng rng(9);
    std::string random(512, '\0');
    for (auto& c : random)
        c = char(rng.below(256));
    CHECK(!deserializePatterns(random, probe));
    CHECK(std::memcmp(&bank, &probe, sizeof(PatternBank)) == 0);
}

void testParams()
{
    std::printf("params: defaults in range, labels, division lookup\n");
    const ParamDesc* table = paramTable();
    for (int i = 0; i < kNumParams; ++i) {
        CHECK(table[i].def >= table[i].min && table[i].def <= table[i].max);
        char text[64];
        for (double v = table[i].min; v <= table[i].max; v += 1.0) {
            formatParam(i, v, text, sizeof(text));
            CHECK(text[0] != '\0');
        }
    }
    char text[64];
    formatParam(trackParamIndex(0, kTrackTranspose), 3, text, sizeof(text));
    CHECK(std::strcmp(text, "+3 st") == 0);
    formatParam(trackParamIndex(0, kTrackDivision), 7, text, sizeof(text));
    CHECK(std::strcmp(text, "1/8T") == 0);
    CHECK(divisionForBeats(divisionBeats(4) / 2) == 5);    // 1/16 -> 1/32
    CHECK(divisionForBeats(divisionBeats(5) / 2) == 10);   // 1/32 -> 1/64
    CHECK(divisionForBeats(divisionBeats(9) / 2) == -1);   // no 1/64T
    CHECK(divisionForBeats(divisionBeats(6) * 2) == -1);   // no 1/2T
}

void testFuzz()
{
    std::printf("fuzz: random tempo, seeks, block sizes and settings keep the event contract\n");
    Rng rng(1234);
    Sequencer seq;
    seq.reset(44100.0);
    Recorder rec;
    uint64_t frame = 0;
    double beat = 0.0;
    double tempo = 120.0;
    bool playing = true;
    for (int i = 0; i < 4000; ++i) {
        if (rng.below(50) == 0) {
            double values[kNumParams];
            const ParamDesc* table = paramTable();
            for (int p = 0; p < kNumParams; ++p)
                values[p] = table[p].min + double(rng.below(uint32_t(table[p].max - table[p].min + 1)));
            seq.setSettings(settingsFromValues(values));
        }
        if (rng.below(40) == 0)
            beat = double(rng.below(4000)) / 7.0 - 50.0;   // seek, including negative positions
        if (rng.below(40) == 0)
            tempo = 20.0 + double(rng.below(600));
        if (rng.below(60) == 0)
            playing = !playing;
        if (rng.below(30) == 0)
            seq.setTranspose(int(rng.below(97)) - 48);
        const uint32_t n = 1 + rng.below(2048);
        TransportState tr;
        tr.playing = playing;
        tr.beat = beat;
        tr.tempo = tempo;
        rec.beginBlock(frame, n);
        seq.render(tr, 0, n, rec);
        frame += n;
        beat += n * tempo / 60.0 / 44100.0;
    }
    stop(seq, rec, frame);
    stop(seq, rec, frame + 256);
    CHECK(rec.orderOk);
    int balance = 0;
    for (const auto& e : rec.events) {
        balance += e.on ? 1 : -1;
        CHECK(e.key < 128 && e.track < kNumTracks);
    }
    CHECK(balance == 0);
    CHECK(rec.events.size() > 100);
}

// The clock input, fuzzed, in episodes on a fresh follower: random gaps (exactly four periods and
// one sample around it among them), RUN starts (skipNextGap), PPQN changes, resets, RUN low, then
// one long silence. Invariants: the transport stays finite, never goes back while it plays (between
// resets and PPQN changes), keeps a sane tempo, and has stopped once the silence reaches four
// times the longest gap it may have measured (or 2 s while no period is known).
void testFuzzClock()
{
    std::printf("fuzz: random clock gaps, RUN starts, PPQN changes, then a silence: the transport stays sane\n");
    Rng rng(4321);
    const double sr = 48000.0;
    int64_t monotonicChecks = 0, stoppedChecks = 0;
    for (int episode = 0; episode < 20; ++episode) {
        ClockFollower clock;
        clock.setPpqn(1);
        double lastBeat = -1e9;
        int regular = 24000;   // the last ordinary gap
        int maxGap = 0;        // the longest gap the follower may have measured
        int toNext = 1 + int(rng.below(48000));
        const int64_t silenceAt = 8 * 48000 + int64_t(rng.below(8 * 48000));
        bool silent = false;
        int64_t since = 0;
        for (int64_t i = 0; i < silenceAt + 14 * 48000; ++i) {
            bool pulse = false;
            if (!silent && --toNext <= 0) {
                pulse = true;
                const uint32_t r = rng.below(16);
                int gap = 2000 + int(rng.below(30000));
                if (r == 0)
                    gap = 4 * regular;        // exactly four periods: not a stop
                else if (r == 1)
                    gap = 4 * regular + 1;    // just beyond: a stop
                else if (r == 2)
                    gap = 4 * regular - 1;
                else
                    regular = gap;
                toNext = gap;
                if (i + gap >= silenceAt)
                    silent = true;            // the last pulse before the silence
            }
            if (pulse) {
                maxGap = std::max(maxGap, int(since) + 1);   // a gap, counted as the follower does
                since = 0;
            } else {
                ++since;
            }
            if (rng.below(100000) == 0) {
                clock.reset();
                lastBeat = -1e9;
            }
            if (rng.below(150000) == 0) {
                static const int kPpqn[] = {1, 2, 4, 8, 12, 24, 48};
                clock.setPpqn(kPpqn[rng.below(7)]);
                lastBeat = -1e9;   // a new resolution may move the position
            }
            if (rng.below(50000) == 0)
                clock.skipNextGap();
            const bool run = rng.below(200000) != 0;
            const TransportState tr = clock.tick(pulse, run, sr);
            CHECK(std::isfinite(tr.beat) && std::isfinite(tr.tempo));
            CHECK(tr.tempo > 0.0 && tr.tempo < 1e6);
            if (tr.playing) {
                if (lastBeat > -1e8) {
                    CHECK(tr.beat >= lastBeat - 1e-9);
                    ++monotonicChecks;
                }
                lastBeat = tr.beat;
            }
            // `since` + 1 samples since the last pulse: stopped from four times the gap on.
            if (silent && since >= std::max<int64_t>(4 * int64_t(maxGap), int64_t(2 * sr))) {
                CHECK(!tr.playing);
                ++stoppedChecks;
            }
        }
    }
    CHECK(monotonicChecks > 1000000);
    CHECK(stoppedChecks > 20 * 48000);
}

// Random patterns (every field, drum hits included) played in random blocks keep notes paired,
// and the saved state round-trips: saving what was loaded gives the same text.
void testFuzzPatterns()
{
    std::printf("fuzz: random patterns keep notes paired; the saved state round-trips\n");
    Rng rng(777);
    long events = 0;
    for (int round = 0; round < 30; ++round) {
        PatternBank bank;
        for (auto& t : bank.tracks) {
            for (auto& s : t.steps) {
                s.active = rng.below(3) == 0;
                s.numNotes = uint8_t(1 + rng.below(kMaxVoices));
                for (auto& d : s.degree)
                    d = int8_t(int(rng.below(29)) - 14);
                s.velocity = uint8_t(1 + rng.below(127));
                s.gate = rng.below(8) == 0 ? kGateTie : uint8_t(1 + rng.below(100));
                s.probability = uint8_t(rng.below(101));
                s.ratchet = uint8_t(1 + rng.below(kMaxRatchet));
                s.condition = uint8_t(rng.below(kCondCount));
                s.condB = uint8_t(1 + rng.below(8));
                s.condA = uint8_t(1 + rng.below(s.condB));
                s.hits = uint8_t(rng.below(256));
                for (auto& h : s.drum) {
                    h.velocity = uint8_t(1 + rng.below(127));
                    h.gate = rng.below(8) == 0 ? kGateTie : uint8_t(1 + rng.below(100));
                    h.probability = uint8_t(rng.below(101));
                    h.ratchet = uint8_t(1 + rng.below(kMaxRatchet));
                    h.condition = uint8_t(rng.below(kCondCount));
                    h.condB = uint8_t(1 + rng.below(8));
                    h.condA = uint8_t(1 + rng.below(h.condB));
                }
            }
            for (auto& r : t.rows) {
                r.level = uint8_t(rng.below(101));
                r.probability = uint8_t(rng.below(101));
                r.mute = rng.below(4) == 0;
            }
        }
        const std::string text = serializePatterns(bank);
        PatternBank back;
        CHECK(deserializePatterns(text, back));
        CHECK(serializePatterns(back) == text);

        Sequencer seq;
        seq.reset(48000.0);
        double values[kNumParams];
        defaultParamValues(values);
        const ParamDesc* table = paramTable();
        for (int p = 0; p < kNumParams; ++p)
            if (rng.below(3) == 0)
                values[p] = table[p].min + double(rng.below(uint32_t(table[p].max - table[p].min + 1)));
        for (int t = 0; t < kNumTracks; ++t)
            values[trackParamIndex(t, kTrackEnabled)] = 1;
        seq.setSettings(settingsFromValues(values));
        seq.setPatterns(back);
        Recorder rec;
        uint64_t frame = 0;
        double beat = 0.0;
        for (int i = 0; i < 400; ++i) {
            const uint32_t n = 1 + rng.below(1024);
            TransportState tr;
            tr.playing = true;
            tr.beat = beat;
            tr.tempo = 60.0 + double(rng.below(200));
            rec.beginBlock(frame, n);
            seq.render(tr, 0, n, rec);
            frame += n;
            beat += n * tr.tempo / 60.0 / 48000.0;
        }
        stop(seq, rec, frame);
        stop(seq, rec, frame + 256);
        CHECK(rec.orderOk);
        // Paired note by note: every on of a track's key before its off, and none left sounding.
        bool sounding[kNumTracks][128] = {};
        int errors = 0;
        for (const auto& e : rec.events) {
            bool& on = sounding[e.track][e.key];
            errors += e.on == on;
            on = e.on;
        }
        for (const auto& t : sounding)
            for (bool b : t)
                errors += b;
        CHECK(errors == 0);
        events += long(rec.events.size());
    }
    CHECK(events > 3000);   // not an empty proof
}

} // namespace

int main()
{
    testGrid();
    testTempoAndOffset();
    testSwing();
    testSwingEarly();
    testRatchet();
    testDrunk();
    testLongPattern();
    testConditions();
    testStartWindow();
    testGrid16x8();
    testClockFollower();
    testClockRestart();
    testClockRestartCases();
    testPpqnChangeKeepsPosition();
    testDrunkRewind();
    testLaneKeepsNewerNote();
    testDrumMode();
    testDrumGrid();
    testStateVersion3();
    testCvVoices();
    testFastPathEquivalence();
    testStepFields();
    testDrumLanes();
    testCvFrameFastPath();
    testEditOps();
    testJitter();
    testStop();
    testMonoLegato();
    testDirections();
    testPolyAndScale();
    testTranspose();
    testGenerator();
    testState();
    testParams();
    testFuzz();
    testFuzzClock();
    testFuzzPatterns();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all core tests passed\n");
    return 0;
}

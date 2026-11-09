// Module tests of the RUN input (2.7.1): the real Tetrad (src/ and core/, the plugin's own
// objects) linked against the Rack SDK's libRack and driven sample by sample, without a window or
// a running Rack (an engine only where Rack's own code needs one).

// Initialize goes through Rack's engine, whose constructor the SDK keeps private: declared here
// first, without the attribute that forbids it to plugins (a test, not the plugin, creates it).
#define PRIVATE
#include <context.hpp>
#include <engine/Engine.hpp>
#include <history.hpp>
#undef PRIVATE
#include "../src/Tetrad.hpp"

#include <algorithm>
#include <atomic>
#include <utility>
#include <vector>
#include <functional>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <thread>

// Allocations made inside process() (the release bar: none).
static thread_local bool gInProcess = false;
static std::atomic<long> gProcessAllocs{0};
static std::atomic<long> gLive{0};   // allocations not yet freed (the removal test)
void* operator new(size_t n)
{
    if (gInProcess)
        ++gProcessAllocs;
    if (void* p = std::malloc(n ? n : 1)) {
        ++gLive;
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept
{
    if (p)
        --gLive;
    std::free(p);
}
void operator delete(void* p, size_t) noexcept
{
    if (p)
        --gLive;
    std::free(p);
}

namespace {

int failures = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

struct Rig {
    std::unique_ptr<Tetrad> m;
    float rate;
    int64_t frame = 0;
    explicit Rig(float sr = 48000.f) : m(std::make_unique<Tetrad>()), rate(sr)
    {
        m->model = modelTetrad;
        Module::SampleRateChangeEvent e{sr, 1.f / sr};
        m->onSampleRateChange(e);
    }
    void step()
    {
        Module::ProcessArgs a{rate, 1.f / rate, frame++};
        gInProcess = true;
        m->process(a);
        gInProcess = false;
    }
    void steps(int n)
    {
        for (int i = 0; i < n; ++i)
            step();
    }
    void connect(int input, float v)
    {
        m->inputs[input].channels = 1;   // Rack: setChannels() is ignored on a disconnected port
        m->inputs[input].setVoltage(v);
    }
    bool running() const { return m->running.load(); }
};

constexpr int kRun = Tetrad::RUN_INPUT;
constexpr int kPulse = 48;   // 1 ms at 48 kHz: the short trigger of a clock module
constexpr int kPastGuard = 64;   // more than Tetrad's start-up guard on RUN edges

// Loads `json` as the module's data, as Rack does when it opens a patch.
void load(Tetrad* m, const char* json)
{
    json_error_t err;
    json_t* j = json_loads(json, 0, &err);
    m->dataFromJson(j);
    json_decref(j);
}

void testNewModule()
{
    std::printf("run, a new Tetrad: trigger mode, playing, so Clocked's first pulse (stop) pauses it\n");
    Rig r;
    CHECK(r.m->runMode.load() == Tetrad::kRunTrigger);
    CHECK(r.m->runLatch.load());
    r.connect(kRun, 0.f);
    r.steps(kPastGuard);
    CHECK(r.running());
    r.connect(kRun, 10.f);
    r.steps(kPulse);
    r.connect(kRun, 0.f);
    r.steps(kPulse);
    CHECK(!r.running());
}

// The internal clock plays whenever the RUN decision says so: `running` shows it directly.
void testGateMode()
{
    std::printf("run, a patch from 2.7.0 (no run mode saved): gate, high plays, low pauses, as before\n");
    Rig r;
    load(r.m.get(), "{\"ppqn\": 0}");
    CHECK(r.m->runMode.load() == Tetrad::kRunGate);
    r.steps(10);
    CHECK(r.running());   // unpatched: follows PLAY
    r.connect(kRun, 0.f);
    r.steps(10);
    CHECK(!r.running());
    r.connect(kRun, 5.f);
    r.steps(10);
    CHECK(r.running());
    r.connect(kRun, 0.f);
    r.steps(10);
    CHECK(!r.running());   // a gate: a trigger only plays while it lasts
}

void testTriggerMode()
{
    std::printf("run, trigger mode: each trigger toggles, held high counts once, PLAY still stops\n");
    Rig r;
    r.m->setRunMode(Tetrad::kRunTrigger);
    auto trigger = [&] {
        r.connect(kRun, 10.f);
        r.steps(kPulse);
        r.connect(kRun, 0.f);
        r.steps(kPulse);
    };
    r.connect(kRun, 0.f);
    r.steps(kPastGuard);
    CHECK(r.running());            // starts playing, like Clocked
    gProcessAllocs = 0;
    trigger();
    CHECK(!r.running());           // the clock's stop
    trigger();
    r.steps(4800);
    CHECK(r.running());            // the clock's start; still playing once the pulse has ended
    CHECK(gProcessAllocs.load() == 0);
    trigger();
    r.connect(kRun, 10.f);         // a voltage held high is one trigger, not one per sample
    r.steps(4800);
    CHECK(r.running());
    r.connect(kRun, 0.f);
    r.steps(10);
    CHECK(r.running());
    r.connect(kRun, NAN);          // NaN is neither high nor low: no toggle
    r.steps(100);
    r.connect(kRun, 0.f);
    r.steps(10);
    CHECK(r.running());
    r.m->params[Tetrad::PLAY_PARAM].setValue(0.f);   // PLAY off still stops
    r.steps(10);
    CHECK(!r.running());
    r.m->params[Tetrad::PLAY_PARAM].setValue(1.f);
    r.steps(10);
    CHECK(r.running());
    trigger();
    CHECK(!r.running());
    r.m->inputs[kRun].channels = 0;   // unpatched: follows PLAY, as in gate mode
    r.steps(10);
    CHECK(r.running());
}

void testSwitchWhileHigh()
{
    std::printf("run: switching to trigger mode while RUN is high is not a trigger\n");
    Rig r;
    load(r.m.get(), "{\"ppqn\": 0}");   // a 2.7.0 patch: gate mode
    r.connect(kRun, 10.f);
    r.steps(kPastGuard);
    CHECK(r.running());
    r.m->setRunMode(Tetrad::kRunTrigger);   // playing (a new mode starts playing)
    r.steps(4800);
    CHECK(r.running());   // the high RUN did not toggle it off
    r.connect(kRun, 0.f);
    r.steps(10);
    r.connect(kRun, 10.f);   // a real rising edge, now
    r.steps(kPulse);
    CHECK(!r.running());
}

void testLoadGuard()
{
    std::printf("run, trigger mode: a source already high when a patch loads is not a trigger\n");
    // Rack delivers a cable's voltage one sample late: the first sample reads 0 V.
    Rig r;
    r.m->setRunMode(Tetrad::kRunTrigger);
    r.connect(kRun, 0.f);
    r.step();
    r.connect(kRun, 10.f);
    r.steps(200);
    CHECK(r.running());            // no toggle
    r.connect(kRun, 0.f);
    r.steps(10);
    r.connect(kRun, 10.f);         // a real pulse, once the guard is over, still toggles
    r.steps(kPulse);
    CHECK(!r.running());
}

// Track 1 on, one step per quarter note, its 16 first steps on.
void fill16(Tetrad* m)
{
    m->params[trackParamIndex(0, kTrackEnabled)].setValue(1.f);
    m->params[trackParamIndex(0, kTrackDivision)].setValue(2.f);   // 1/4
    m->editPatterns([](PatternBank& b) {
        for (int i = 0; i < 16; ++i) {
            b.tracks[0].steps[i].active = true;
            b.tracks[0].steps[i].gate = 50;
            b.tracks[0].steps[i].degree[0] = int8_t(i);
        }
        return true;
    });
}

void testStartWaitsForClock()
{
    std::printf("run, trigger mode: a start over a clock held high waits for the clock's next pulse\n");
    // A patch saved while Impromptu Clocked was stopped: Tetrad paused, the clock held high. The
    // cable reads 0 V on the first sample, so Tetrad sees an edge as the patch opens.
    Rig r;
    fill16(r.m.get());
    r.m->runLatch = false;
    r.connect(kRun, 0.f);
    r.connect(Tetrad::CLOCK_INPUT, 0.f);
    r.step();
    r.connect(Tetrad::CLOCK_INPUT, 10.f);
    r.steps(48000);
    auto gateHigh = [&] { return r.m->outputs[Tetrad::GATE_OUTPUT].getVoltage() > 1.f; };
    CHECK(!gateHigh());
    r.connect(kRun, 10.f);   // the clock's start: no clock edge comes with it
    r.steps(kPulse);
    r.connect(kRun, 0.f);
    bool early = false;
    for (int i = 0; i < 19200; ++i) {   // 0.4 s, before the clock's next pulse
        r.step();
        early = early || gateHigh();
    }
    CHECK(!early);   // no step played on its own, off the beat
    r.connect(Tetrad::CLOCK_INPUT, 0.f);
    r.steps(240);
    r.connect(Tetrad::CLOCK_INPUT, 10.f);   // the clock's pulse
    r.steps(10);
    CHECK(gateHigh());   // the step plays on it
}

// A patch opening over a clock held high (a stopped Impromptu Clocked): the cable reads 0 V on its
// first sample, an edge that counts. Then the clock starts, a pulse every 0.5 s. `holdSeconds`
// later; `withRun`: a RUN cable in trigger mode, its pulse at the start.
int attacksAfterOpening(double holdSeconds, bool withRun, int& edges)
{
    Rig r;
    fill16(r.m.get());
    if (withRun) {
        r.m->runLatch = false;   // saved paused
        r.connect(kRun, 0.f);
    }
    r.connect(Tetrad::CLOCK_INPUT, 0.f);
    r.step();
    r.connect(Tetrad::CLOCK_INPUT, 10.f);
    r.steps(int(holdSeconds * 48000) - 1);
    if (withRun) {
        r.connect(kRun, 10.f);
        r.steps(kPulse);
        r.connect(kRun, 0.f);
    }
    // Held high until then, the clock's first beat has no rising edge (nothing can see it); every
    // later beat is an edge, and each must strike its step with an attack.
    int rises = 0;
    edges = 0;
    bool high = false, clkBefore = true;
    for (int i = 0; i < 6 * 24000; ++i) {
        const bool clk = i % 24000 < 12000;   // 50 % pulses
        edges += clk && !clkBefore;
        clkBefore = clk;
        r.connect(Tetrad::CLOCK_INPUT, clk ? 10.f : 0.f);
        r.step();
        const bool g = r.m->outputs[Tetrad::GATE_OUTPUT].getVoltage() > 1.f;
        rises += g && !high;
        high = g;
    }
    return rises;
}

void testOpenOverHighClock()
{
    std::printf("clock: a patch opening over a clock held high, then started: every step gets its attack\n");
    int edges = 0;
    // No RUN cable: the start is known by its gap, longer than 2 s (the transport's own timeout).
    int rises = attacksAfterOpening(3.0, false, edges);
    if (rises != edges)
        std::printf("  no RUN, 3 s: %d attacks for %d clock edges\n", rises, edges);
    CHECK(rises == edges);
    // A RUN cable in trigger mode: the start is its pulse, known exactly, however soon.
    rises = attacksAfterOpening(1.5, true, edges);
    if (rises != edges)
        std::printf("  RUN trigger, 1.5 s: %d attacks for %d clock edges\n", rises, edges);
    CHECK(rises == edges);
}

void testFirstBeatOnOpening()
{
    std::printf("opening: a clock striking on the first samples plays step 1 (patterns in place)\n");
    // A patch's patterns arrive before the engine runs the module (dataFromJson, here editPatterns
    // as the UI would): its first control pass must come before its first beat.
    Rig r;
    fill16(r.m.get());
    r.connect(Tetrad::CLOCK_INPUT, 0.f);   // the cable's first sample
    r.step();
    r.connect(Tetrad::CLOCK_INPUT, 10.f);   // the clock's first pulse
    r.steps(4);
    CHECK(r.m->outputs[Tetrad::GATE_OUTPUT].getVoltage() > 1.f);
    CHECK(std::fabs(r.m->outputs[Tetrad::VOCT_OUTPUT].getVoltage()) < 1e-4f);   // degree 0: step 1
}

void testSavedState()
{
    std::printf("run, saved data: mode and play / pause survive a save; older patches mean gate\n");
    {
        Rig a;
        a.m->setRunMode(Tetrad::kRunTrigger);
        a.m->runLatch = false;
        json_t* j = a.m->dataToJson();
        Rig b;
        b.m->dataFromJson(j);
        json_decref(j);
        CHECK(b.m->runMode.load() == Tetrad::kRunTrigger);
        CHECK(!b.m->runLatch.load());
        b.connect(kRun, 0.f);
        b.steps(kPastGuard);
        CHECK(!b.running());   // reloaded paused, as saved
    }
    {
        Rig r;
        struct Case {
            const char* json;
            int mode;
        };
        // From a module in trigger mode: no key (2.7.0 and earlier) means the gate; a saved mode
        // is read back, clamped; an unreadable one leaves the mode as it is.
        for (const Case& c : {Case{"{\"ppqn\": 0}", 0}, Case{"{\"runMode\": 0}", 0},
                              Case{"{\"runMode\": 1}", 1}, Case{"{\"runMode\": 1.0}", 1},
                              Case{"{\"runMode\": \"0\"}", 1}, Case{"{\"runMode\": 99}", 1},
                              Case{"{\"runMode\": -5}", 0}, Case{"{\"runMode\": null}", 1}}) {
            r.m->runMode = Tetrad::kRunTrigger;
            load(r.m.get(), c.json);
            if (r.m->runMode.load() != c.mode)
                std::printf("  %s -> run mode %d\n", c.json, r.m->runMode.load());
            CHECK(r.m->runMode.load() == c.mode);
        }
        r.m->runLatch = true;
        load(r.m.get(), "{\"runMode\": 1, \"runLatch\": \"no\"}");
        CHECK(r.m->runLatch.load());
    }
    {
        // Play / pause comes back with a patch (its module has an "id"), not with a preset or
        // a paste: one saved paused, loaded while Tetrad plays, must not pause it.
        // Module::fromJson looks the plugin and model up by slug: no plugin is loaded here, so a
        // stand-in carries the model for this block (taken back before it goes: ~Plugin deletes
        // the models it still holds).
        plugin::Plugin standIn;
        standIn.slug = "Fourfold";
        standIn.models.push_back(modelTetrad);
        modelTetrad->plugin = &standIn;
        plugin::plugins.push_back(&standIn);
        Rig r;
        r.m->setRunMode(Tetrad::kRunTrigger);
        const std::string head = "{\"plugin\": \"Fourfold\", \"model\": \"Tetrad\", ";
        const std::string data = "\"data\": {\"runMode\": 1, \"runLatch\": false}}";
        json_error_t err;
        json_t* preset = json_loads((head + data).c_str(), 0, &err);
        r.m->fromJson(preset);
        json_decref(preset);
        CHECK(r.m->runLatch.load());
        json_t* patch = json_loads((head + "\"id\": 7, " + data).c_str(), 0, &err);
        r.m->fromJson(patch);
        json_decref(patch);
        CHECK(!r.m->runLatch.load());
        plugin::plugins.pop_back();
        modelTetrad->plugin = nullptr;
        standIn.models.clear();
    }
}

void testMenuAndTooltip()
{
    std::printf("run, menu and tooltip: a new mode starts playing; one tooltip for both modes\n");
    Rig r;
    PortInfo* info = r.m->inputInfos[kRun];
    const std::string name = info->getName();   // one tooltip, the same in both modes
    CHECK(name.find("trigger or gate") != std::string::npos);
    r.m->setRunMode(Tetrad::kRunGate);
    CHECK(info->getName() == name);
    r.m->runLatch = false;
    r.m->setRunMode(Tetrad::kRunTrigger);
    CHECK(r.m->runLatch.load());
    CHECK(info->getName() == name);
    r.m->runLatch = false;
    r.m->setRunMode(Tetrad::kRunTrigger);   // the same mode again: nothing changes
    CHECK(!r.m->runLatch.load());
    r.m->setRunMode(Tetrad::kRunGate);
    r.m->setRunMode(Tetrad::kRunTrigger);
    CHECK(r.m->runLatch.load());            // Trigger, Gate, Trigger: playing again
    r.m->setRunMode(7);
    CHECK(r.m->runMode.load() == Tetrad::kNumRunModes - 1);
    // Initialize (onReset) puts trigger mode back, as a new module; it needs an engine
    // (Module::onReset sets the parameters through it), so it is not driven here.
}

} // namespace


// Tetrad and a Tetrad Pipe on its right, as in a patch: the Pipe reads Tetrad's frame one sample
// late through Rack's expander messages, flipped here after each sample as the engine does.
struct PipeRig {
    Rig r;
    std::unique_ptr<Module> pipe;
    explicit PipeRig(float sr = 48000.f) : r(sr), pipe(modelTetradPipe->createModule())
    {
        for (auto& o : pipe->outputs)
            o.channels = 1;
        r.m->rightExpander.module = pipe.get();
        pipe->leftExpander.module = r.m.get();
    }
    void step()
    {
        r.step();
        Module::ProcessArgs a{r.rate, 1.f / r.rate, r.frame};
        pipe->process(a);
        if (pipe->leftExpander.messageFlipRequested) {
            std::swap(pipe->leftExpander.producerMessage, pipe->leftExpander.consumerMessage);
            pipe->leftExpander.messageFlipRequested = false;
        }
    }
    float out(int i) { return pipe->outputs[i].getVoltage(); }
};

// Pulses on one output over `n` samples: their count, and the shortest and longest widths.
struct Pulses {
    int count = 0, shortest = 1 << 30, longest = 0, run = 0;
    float top = 0.f;
    void add(float v)
    {
        if (v > 1.f) {
            ++run;
            top = std::max(top, v);
        } else if (run) {
            ++count;
            shortest = std::min(shortest, run);
            longest = std::max(longest, run);
            run = 0;
        }
    }
};

void testOutputLevels()
{
    std::printf("outputs: gates 10 V; triggers, clock and reset 10 V for 1 ms; V/OCT 0 V = C4\n");
    PipeRig p;
    fill16(p.r.m.get());   // track 1, 1/4 steps, degree i on step i (C minor, octave 4)
    Pulses gate, trig, clk;
    float firstPitch = NAN;
    bool high = false;
    for (int i = 0; i < 4 * 24000; ++i) {   // 2 s at 120 BPM: four steps
        p.step();
        const float g = p.r.m->outputs[Tetrad::GATE_OUTPUT].getVoltage();
        gate.add(g);
        if (g > 1.f && !high && std::isnan(firstPitch))
            firstPitch = p.r.m->outputs[Tetrad::VOCT_OUTPUT].getVoltage();
        high = g > 1.f;
        trig.add(p.out(12));   // Pipe output 13: trigger of track 1
        clk.add(p.out(14));    // Pipe output 15: clock
    }
    CHECK(gate.count >= 3 && gate.top == 10.f);
    CHECK(firstPitch == 0.f);   // step 1, degree 0 of C minor at octave 4: C4
    CHECK(trig.count >= 3 && trig.top == 10.f && trig.shortest >= 47 && trig.longest <= 49);
    CHECK(clk.count >= 3 && clk.top == 10.f && clk.shortest >= 47 && clk.longest <= 49);
    if (!(trig.shortest >= 47 && trig.longest <= 49 && clk.shortest >= 47 && clk.longest <= 49))
        std::printf("  trigger %d..%d samples, clock %d..%d samples\n", trig.shortest, trig.longest,
                    clk.shortest, clk.longest);
    // RESET pulses the Pipe's reset output, 10 V for 1 ms.
    Pulses rst;
    p.r.connect(Tetrad::RESET_INPUT, 10.f);
    for (int i = 0; i < 480; ++i) {
        if (i == 48)
            p.r.connect(Tetrad::RESET_INPUT, 0.f);
        p.step();
        rst.add(p.out(15));
    }
    CHECK(rst.count == 1 && rst.top == 10.f && rst.shortest >= 47 && rst.longest <= 49);
}

// The steps a clock signal makes Tetrad play (gate rises of track 1, 1/16 steps at 4 PPQN).
int stepsFrom(const std::function<float(int)>& clock, int n)
{
    Rig r;
    load(r.m.get(), "{\"ppqn\": 2}");   // 4 PPQN: one step per pulse
    fill16(r.m.get());
    r.m->params[trackParamIndex(0, kTrackDivision)].setValue(4.f);   // 1/16
    int rises = 0;
    bool high = false;
    for (int i = 0; i < n; ++i) {
        r.connect(Tetrad::CLOCK_INPUT, clock(i));
        r.step();
        const bool g = r.m->outputs[Tetrad::GATE_OUTPUT].getVoltage() > 1.f;
        rises += g && !high;
        high = g;
    }
    return rises;
}

void testInputThresholds()
{
    std::printf("inputs: CLOCK edges from 2 V, with hysteresis down to 0.1 V; RESET the same\n");
    // Ten pulses, 6000 samples apart (4 PPQN at 120 BPM), at a given top voltage and floor.
    auto pulses = [](float top, float floor) {
        return [=](int i) { return i >= 6000 && i < 66000 && i % 6000 < 48 ? top : floor; };
    };
    const int full = stepsFrom(pulses(10.f, 0.f), 72000);
    CHECK(full >= 9);
    CHECK(stepsFrom(pulses(2.f, 0.f), 72000) == full);    // 2 V counts
    CHECK(stepsFrom(pulses(1.9f, 0.f), 72000) == 0);      // below the threshold: nothing
    CHECK(stepsFrom(pulses(10.f, 0.5f), 72000) <= 1);     // never back under 0.1 V: one edge
    // Ringing on a pulse (10 V, a dip to 0.5 V, 10 V again) is one edge, not two.
    auto ringing = [](int i) {
        if (i < 6000 || i >= 66000)
            return 0.f;
        const int k = i % 6000;
        return k < 20 ? 10.f : k < 24 ? 0.5f : k < 48 ? 10.f : 0.f;
    };
    CHECK(stepsFrom(ringing, 72000) == full);
    // RESET: a pulse at a given voltage, seen on the Pipe's reset output (10 V for 1 ms). The
    // output also pulses when the transport starts, on the first samples: counted from 0.01 s on.
    auto resets = [](float top) {
        PipeRig p;
        Pulses rst;
        for (int i = 0; i < 4800; ++i) {
            p.r.connect(Tetrad::RESET_INPUT, i >= 960 && i < 1008 ? top : 0.f);
            p.step();
            if (i >= 480)
                rst.add(p.out(15));
        }
        if (top >= 2.f && rst.count != 1)
            std::printf("  RESET at %.1f V: %d reset pulse(s)\n", double(top), rst.count);
        return rst.count;
    };
    CHECK(resets(2.f) == 1);
    CHECK(resets(1.9f) == 0);
}

void testNaNInputs()
{
    std::printf("inputs: NaN and infinity on every input give finite outputs\n");
    PipeRig p;
    fill16(p.r.m.get());
    const float bad[] = {NAN, INFINITY, -INFINITY, NAN};
    bool finite = true;
    for (int i = 0; i < 48000; ++i) {
        for (int in = 0; in < Tetrad::NUM_INPUTS; ++in)
            p.r.connect(in, bad[(i / 1000 + in) % 4]);
        p.step();
        for (auto& o : p.r.m->outputs)
            for (int c = 0; c < std::max(1, o.getChannels()); ++c)
                finite = finite && std::isfinite(o.getVoltage(c));
        for (auto& o : p.pipe->outputs)
            for (int c = 0; c < std::max(1, o.getChannels()); ++c)
                finite = finite && std::isfinite(o.getVoltage(c));
    }
    CHECK(finite);
}

void testResetHoldoff()
{
    std::printf("reset: CLOCK ignored for 1 ms after RESET, counted after\n");
    // A pulse train, a RESET, then one clock edge `delay` samples after it: does it play step 1?
    auto stepAfterReset = [](int delay) {
        Rig r;
        load(r.m.get(), "{\"ppqn\": 2}");
        fill16(r.m.get());
        r.m->params[trackParamIndex(0, kTrackDivision)].setValue(4.f);
        for (int i = 0; i < 24000; ++i) {   // a running clock first
            r.connect(Tetrad::CLOCK_INPUT, i % 6000 < 48 ? 10.f : 0.f);
            r.step();
        }
        r.connect(Tetrad::CLOCK_INPUT, 0.f);
        r.steps(3000);
        r.connect(Tetrad::RESET_INPUT, 10.f);
        bool rose = false, high = r.m->outputs[Tetrad::GATE_OUTPUT].getVoltage() > 1.f;
        for (int i = 0; i < 4000; ++i) {
            if (i == 48)
                r.connect(Tetrad::RESET_INPUT, 0.f);
            r.connect(Tetrad::CLOCK_INPUT, i >= delay && i < delay + 48 ? 10.f : 0.f);
            r.step();
            const bool g = r.m->outputs[Tetrad::GATE_OUTPUT].getVoltage() > 1.f;
            rose = rose || (g && !high);
            high = g;
        }
        return rose;
    };
    CHECK(!stepAfterReset(0));    // the same sample as RESET
    CHECK(!stepAfterReset(1));    // one cable later
    CHECK(!stepAfterReset(24));   // 0.5 ms
    CHECK(stepAfterReset(96));    // 2 ms: counted, step 1 plays
}

void testHighSampleRate()
{
    std::printf("sample rates: 44.1, 96 and 192 kHz play the same steps at the same times\n");
    // The internal clock at 120 BPM for 2 s: the gate rises of track 1, in seconds.
    auto rises = [](float sr) {
        Rig r(sr);
        fill16(r.m.get());
        std::vector<double> t;
        bool high = false;
        for (int i = 0; i < int(2 * sr); ++i) {
            r.step();
            const bool g = r.m->outputs[Tetrad::GATE_OUTPUT].getVoltage() > 1.f;
            if (g && !high)
                t.push_back(i / double(sr));
            high = g;
        }
        return t;
    };
    const std::vector<double> ref = rises(48000.f);
    CHECK(ref.size() >= 3);
    for (float sr : {44100.f, 96000.f, 192000.f}) {
        const std::vector<double> t = rises(sr);
        bool same = t.size() == ref.size();
        for (size_t k = 0; same && k < t.size(); ++k)
            same = std::fabs(t[k] - ref[k]) < 2.5e-4;   // within a quarter of a millisecond
        if (!same)
            std::printf("  %.0f Hz: %zu steps (48 kHz: %zu)\n", double(sr), t.size(), ref.size());
        CHECK(same);
    }
}

void testRemoval()
{
    std::printf("removal: a playing Tetrad goes in well under 50 ms and frees what it took\n");
    auto once = []() {
        Rig r;
        fill16(r.m.get());
        r.steps(4800);
        const auto a = std::chrono::steady_clock::now();
        r.m.reset();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
    };
    once();   // the first one sets up what Rack keeps for good (models, fonts...)
    double worst = 0;
    const long before = gLive.load();
    for (int k = 0; k < 20; ++k)
        worst = std::max(worst, once());
    const long leaked = gLive.load() - before;
    if (worst >= 50.0 || leaked != 0)
        std::printf("  worst %.2f ms, %ld allocation(s) left\n", worst, leaked);
    CHECK(worst < 50.0);
    CHECK(leaked == 0);
}

void testInitialize()
{
    std::printf("Initialize: trigger mode and playing, as a new Tetrad, even on a 2.7.0 patch; tracks empty\n");
    // Module::onReset sets the parameters through the engine: a minimal one, in a context, kept
    // until the program ends (its destructor is private too). No module is added to it.
    if (!contextGet())
        contextSet(new Context);
    if (!APP->engine)
        APP->engine = new engine::Engine;
    Rig r;
    load(r.m.get(), "{\"ppqn\": 0, \"runLatch\": false}");   // a 2.7.0 patch (gate), paused
    fill16(r.m.get());
    r.steps(64);
    CHECK(r.m->runMode.load() == Tetrad::kRunGate);
    Module::ResetEvent e;
    r.m->onReset(e);
    r.steps(64);
    CHECK(r.m->runMode.load() == Tetrad::kRunTrigger);
    CHECK(r.m->runLatch.load());
    PatternBank bank;
    r.m->copyPatterns(bank);
    int active = 0;
    for (const auto& t : bank.tracks)
        for (const auto& st : t.steps)
            active += st.active || st.hits;
    CHECK(active == 0);
}

void testRunModeUndo()
{
    std::printf("run, menu: a new RUN mode is one undo entry (Ctrl+Z, Ctrl+Shift+Z)\n");
    // The menu's code path, with the engine and history Rack has (kept until the program ends).
    if (!contextGet())
        contextSet(new Context);
    if (!APP->engine)
        APP->engine = new engine::Engine;
    if (!APP->history)
        APP->history = new history::State;
    Rig r;
    APP->engine->addModule(r.m.get());
    CHECK(r.m->runMode.load() == Tetrad::kRunTrigger);
    r.m->changeRunMode(Tetrad::kRunGate);
    CHECK(r.m->runMode.load() == Tetrad::kRunGate);
    r.m->changeRunMode(Tetrad::kRunGate);   // the same mode again: no new entry
    APP->history->undo();
    CHECK(r.m->runMode.load() == Tetrad::kRunTrigger);
    APP->history->redo();
    CHECK(r.m->runMode.load() == Tetrad::kRunGate);
    APP->history->undo();
    CHECK(r.m->runMode.load() == Tetrad::kRunTrigger);
    // Paused in trigger mode, then Gate, then undo: trigger mode, and paused again.
    r.m->runLatch = false;
    r.m->changeRunMode(Tetrad::kRunGate);
    CHECK(r.m->runLatch.load());
    APP->history->undo();
    CHECK(r.m->runMode.load() == Tetrad::kRunTrigger && !r.m->runLatch.load());
    APP->history->redo();
    CHECK(r.m->runMode.load() == Tetrad::kRunGate && r.m->runLatch.load());
    APP->engine->removeModule(r.m.get());
}

void testPresetKeepsRunMode()
{
    std::printf("run, presets: one from 2.7.0 (no run mode) keeps the module's; a patch from 2.7.0 opens in Gate\n");
    plugin::Plugin standIn;
    standIn.slug = "Fourfold";
    standIn.models.push_back(modelTetrad);
    modelTetrad->plugin = &standIn;
    plugin::plugins.push_back(&standIn);
    auto apply = [](Tetrad* m, const std::string& json) {
        json_error_t err;
        json_t* j = json_loads(json.c_str(), 0, &err);
        m->fromJson(j);
        json_decref(j);
    };
    const std::string head = "{\"plugin\": \"Fourfold\", \"model\": \"Tetrad\", ";
    {
        Rig r;   // trigger mode, as a new Tetrad under Clocked
        apply(r.m.get(), head + "\"data\": {\"ppqn\": 0}}");   // a preset saved with 2.7.0
        CHECK(r.m->runMode.load() == Tetrad::kRunTrigger);
        apply(r.m.get(), head + "\"data\": {\"ppqn\": 0, \"runMode\": 0}}");   // one from 2.7.1, Gate
        CHECK(r.m->runMode.load() == Tetrad::kRunGate);
    }
    {
        Rig r;
        apply(r.m.get(), head + "\"id\": 3, \"data\": {\"ppqn\": 0}}");   // a patch from 2.7.0
        CHECK(r.m->runMode.load() == Tetrad::kRunGate);
    }
    plugin::plugins.pop_back();
    modelTetrad->plugin = nullptr;
    standIn.models.clear();
}

void testRunJustAfterClock()
{
    std::printf("run, trigger: a start one sample after the clock's pulse keeps the tempo\n");
    // 1 PPQN at 120 BPM, 1/16 steps: four steps a beat, 6000 samples apart. Paused (a RUN pulse)
    // with the clock held low for 1 s, then the clock starts again and RUN follows one sample later.
    Rig r;
    fill16(r.m.get());
    r.m->params[trackParamIndex(0, kTrackDivision)].setValue(4.f);   // 1/16
    r.connect(kRun, 0.f);
    auto clockFor = [&](int samples, int from) {
        for (int i = 0; i < samples; ++i) {
            r.connect(Tetrad::CLOCK_INPUT, (from + i) % 24000 < 48 ? 10.f : 0.f);
            r.step();
        }
    };
    clockFor(4 * 24000, 0);   // running
    r.connect(kRun, 10.f);    // stop
    r.steps(kPulse);
    r.connect(kRun, 0.f);
    r.connect(Tetrad::CLOCK_INPUT, 0.f);
    r.steps(48000 - kPulse);  // 1 s paused, the clock low
    CHECK(!r.running());
    std::vector<int> rises;
    bool high = false;
    for (int i = 0; i < 3 * 24000; ++i) {
        r.connect(Tetrad::CLOCK_INPUT, i % 24000 < 48 ? 10.f : 0.f);
        r.connect(kRun, i >= 1 && i < 1 + kPulse ? 10.f : 0.f);   // the start, one sample late
        r.step();
        const bool g = r.m->outputs[Tetrad::GATE_OUTPUT].getVoltage() > 1.f;
        if (g && !high)
            rises.push_back(i);
        high = g;
    }
    // From the next pulse on, four steps a beat, 6000 samples apart (the tempo before the stop).
    bool even = rises.size() >= 5;
    for (size_t k = 1; even && k < rises.size(); ++k)
        even = std::abs(rises[k] - rises[k - 1] - 6000) <= 2;
    if (!even) {
        std::printf("  rises:");
        for (int x : rises)
            std::printf(" %d", x);
        std::printf("\n");
    }
    CHECK(even);
}

void testPanelLock()
{
    std::printf("real time: while the panel holds the pattern lock, the engine goes on; the edit lands after\n");
    Rig r;
    fill16(r.m.get());
    r.steps(64);
    std::atomic<bool> holding{false}, release{false};
    // The panel's thread edits the patterns and keeps the lock until told to let go.
    std::thread panel([&] {
        r.m->editPatterns([&](PatternBank& b) {
            b.tracks[1].steps[0].active = true;
            holding = true;
            while (!release)
                std::this_thread::yield();
            return true;
        });
    });
    while (!holding)
        std::this_thread::yield();
    // A watchdog: an engine that waited for the lock would hang here, so this fails it instead.
    std::atomic<bool> done{false};
    std::thread watchdog([&] {
        for (int i = 0; i < 1000 && !done; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!done) {
            std::printf("  FAIL: the engine waited for the panel's lock\n");
            std::fflush(stdout);
            std::_Exit(1);
        }
    });
    const long allocs = gProcessAllocs.load();
    // 0.75 s with the lock held elsewhere: the engine only tries it, and plays on (1/4 steps).
    int rises = 0;
    bool high = r.m->outputs[Tetrad::GATE_OUTPUT].getVoltage() > 1.f;
    for (int i = 0; i < 36000; ++i) {
        r.step();
        const bool g = r.m->outputs[Tetrad::GATE_OUTPUT].getVoltage() > 1.f;
        rises += g && !high;
        high = g;
    }
    done = true;
    watchdog.join();
    CHECK(rises >= 1);
    CHECK(gProcessAllocs.load() == allocs);
    release = true;
    panel.join();
    r.steps(64);
    PatternBank bank;
    r.m->copyPatterns(bank);
    CHECK(bank.tracks[1].steps[0].active);
    CHECK(bank.tracks[0].steps[15].active);
}

int main()
{
    gProcessAllocs = 0;
    testNewModule();
    testGateMode();
    testTriggerMode();
    testSwitchWhileHigh();
    testLoadGuard();
    testStartWaitsForClock();
    testOpenOverHighClock();
    testFirstBeatOnOpening();
    testSavedState();
    testMenuAndTooltip();
    testPanelLock();
    testInitialize();
    testRunModeUndo();
    testPresetKeepsRunMode();
    testRunJustAfterClock();
    testOutputLevels();
    testInputThresholds();
    testNaNInputs();
    testResetHoldoff();
    testHighSampleRate();
    testRemoval();
    std::printf("real time: no allocation in process() over every test above\n");
    if (gProcessAllocs.load() != 0)
        std::printf("  %ld allocation(s) in process()\n", gProcessAllocs.load());
    CHECK(gProcessAllocs.load() == 0);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all module tests passed\n");
    return 0;
}

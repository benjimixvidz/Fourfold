// A patch from 2.7.0 plays as before: scenarios played by a Tetrad build, every output of every
// sample folded into a hash per phase. Built once against the published 2.7.0 and once against
// this tree (tests/compare_2.7.0.sh): the hashes must be identical, except for the phases that
// 2.7.1 changes on purpose (marked "may differ"). Every module first loads data as a 2.7.0 patch
// has it (no run mode: 2.7.1 keeps the gate). Uses only what 2.7.0 already has.
//   render_dump                  the scenarios
//   render_dump fixture FILE...  patches saved by earlier versions, opened and played

// Rack opens a patch through its engine, whose constructor the SDK keeps private: declared here
// first, without the attribute that forbids it to plugins (a test, not the plugin, creates it).
#define PRIVATE
#include <context.hpp>
#include <engine/Engine.hpp>
#undef PRIVATE
#include "../src/Tetrad.hpp"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float kRate = 48000.f;
constexpr int kBeat = 24000;   // 120 BPM at 48 kHz

struct Hash {
    uint64_t h = 1469598103934665603ull;   // FNV-1a
    long gateSamples = 0;
    long pipeRises = -1;     // Tetrad Pipe phases: rises to 10 V on its outputs (gates, triggers, and a
                             // velocity at its top)
    void add(const void* p, size_t n)
    {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
};

Hash gTotal;

void print(const char* name, const Hash& h)
{
    std::printf("%-40s %016llx  (gate-on samples: %ld)", name, static_cast<unsigned long long>(h.h), h.gateSamples);
    if (h.pipeRises >= 0)
        std::printf(" (pipe rises to 10 V: %ld)", h.pipeRises);
    std::printf("\n");
}

// A module as a 2.7.0 patch opens it, with four tracks on: each its own seed, so the generator
// writes them (a setting moved). Every output patched: Rack ignores setChannels() on an output
// without a cable.
std::unique_ptr<Tetrad> openPatch()
{
    auto m = std::make_unique<Tetrad>();
    m->model = modelTetrad;
    Module::SampleRateChangeEvent e{kRate, 1.f / kRate};
    m->onSampleRateChange(e);
    json_error_t err;
    json_t* patch = json_loads("{\"ppqn\": 0}", 0, &err);   // a 2.7.0 patch's data
    m->dataFromJson(patch);
    json_decref(patch);
    for (int t = 0; t < kNumTracks; ++t) {
        m->params[trackParamIndex(t, kTrackEnabled)].setValue(1.f);
        m->params[trackParamIndex(t, kTrackSeed)].setValue(static_cast<float>(17 + 9 * t));
    }
    m->params[trackParamIndex(2, kTrackMode)].setValue(static_cast<float>(kModePoly));
    m->params[trackParamIndex(3, kTrackMode)].setValue(static_cast<float>(kModeDrum));
    for (auto& o : m->outputs)
        o.channels = 1;
    return m;
}

void connect(Tetrad& m, int input, float v)
{
    m.inputs[input].channels = 1;
    m.inputs[input].setVoltage(v);
}

void unplug(Tetrad& m, int input)
{
    m.inputs[input].channels = 0;
    m.inputs[input].setVoltage(0.f);
}

// One sample: `before` has set the inputs; every output goes into the hash.
void step(Tetrad& m, int64_t& frame, Hash& h)
{
    Module::ProcessArgs a{kRate, 1.f / kRate, frame++};
    m.process(a);
    for (int o = 0; o < Tetrad::NUM_OUTPUTS; ++o) {
        const int c = m.outputs[o].getChannels();
        h.add(&c, sizeof c);
        for (int k = 0; k < c; ++k) {
            const float v = m.outputs[o].getVoltage(k);
            h.add(&v, sizeof v);
            if (o >= Tetrad::GATE_OUTPUT && o < Tetrad::GATE_OUTPUT + kNumTracks && v > 1.f)
                ++h.gateSamples;
        }
    }
}

// `seconds` of playback; `before(i)` sets the inputs of sample i. `inTotal`: folded into TOTAL.
template <typename F>
void phase(Tetrad& m, int64_t& frame, const char* name, float seconds, bool inTotal, F&& before)
{
    Hash h;
    const int n = static_cast<int>(seconds * kRate);
    for (int i = 0; i < n; ++i) {
        before(i);
        step(m, frame, h);
    }
    if (inTotal)
        gTotal.add(&h.h, sizeof h.h);
    print(name, h);
}

// A clock pulse (1 ms, 1 PPQN) at each of the given sample positions.
bool pulseAt(const int* starts, int count, int i)
{
    for (int k = 0; k < count; ++k)
        if (i >= starts[k] && i < starts[k] + 48)
            return true;
    return false;
}

// An external clock on a fresh module, RUN unpatched: `gapAfter(k)` is the gap that follows pulse
// k. The first 0.01 s stay out of the hash (2.7.1 runs its first control pass on the first sample,
// 2.7.0 on the 32nd: the outputs' channels are set that much sooner); the clock starts 0.1 s later.
template <typename G>
void clockScenario(const char* name, float seconds, G&& gapAfter)
{
    auto m = openPatch();
    int64_t frame = 0;
    Hash unused;
    connect(*m, Tetrad::CLOCK_INPUT, 0.f);   // patched from the start: never the internal clock
    for (int i = 0; i < static_cast<int>(0.01f * kRate); ++i)
        step(*m, frame, unused);
    int next = 4800, k = 0;
    phase(*m, frame, name, seconds, true, [&](int i) {
        bool pulse = false;
        if (i >= next && i < next + 48)
            pulse = true;
        if (i == next + 47) {
            next += gapAfter(k);
            ++k;
        }
        connect(*m, Tetrad::CLOCK_INPUT, pulse ? 10.f : 0.f);
    });
}

// Fixture mode: render_dump fixture FILE...  Each file is a module entry as a patch holds it
// (tests/fixtures/), or a whole patch.json (its Tetrad modules). Each is opened as Rack opens a
// patch, then played: tests/compare_2.7.0.sh runs it with 2.7.0 too, the hashes must be equal.
int playFixture(const char* label, json_t* moduleJ)
{
    auto m = std::make_unique<Tetrad>();
    m->model = modelTetrad;
    Module::SampleRateChangeEvent e{kRate, 1.f / kRate};
    m->onSampleRateChange(e);
    m->fromJson(moduleJ);
    for (auto& o : m->outputs)
        o.channels = 1;
    int64_t frame = 0;
    {
        Hash unused;   // the opening, PLAY off (2.7.1 plays the first beat: phase 0a)
        m->params[Tetrad::PLAY_PARAM].setValue(0.f);
        for (int i = 0; i < static_cast<int>(0.01f * kRate); ++i)
            step(*m, frame, unused);
        m->params[Tetrad::PLAY_PARAM].setValue(1.f);
    }
    char name[96];
    std::snprintf(name, sizeof name, "F %s internal clock", label);
    phase(*m, frame, name, 4.f, false, [](int) {});
    std::snprintf(name, sizeof name, "F %s clock + RUN gate", label);
    phase(*m, frame, name, 4.f, false, [&](int i) {
        connect(*m, Tetrad::CLOCK_INPUT, i % kBeat < 48 ? 10.f : 0.f);
        connect(*m, Tetrad::RUN_INPUT, i >= 48000 && i < 96000 ? 0.f : 10.f);
    });
    return 0;
}

int playFixtures(int n, char** paths)
{
    // Module::fromJson looks the plugin and model up by slug: no plugin is loaded here, so a
    // stand-in carries the model (taken back before it goes: ~Plugin deletes the models it holds).
    // Module::fromJson sets the parameters through the engine (ParamQuantity::setImmediateValue):
    // an engine in a context, as Rack has when it opens a patch. No module is added to it.
    // Kept until the program ends, on purpose (the engine's destructor is private too).
    if (!contextGet())
        contextSet(new Context);
    if (!APP->engine)
        APP->engine = new engine::Engine;
    plugin::Plugin standIn;
    standIn.slug = "Fourfold";
    standIn.models.push_back(modelTetrad);
    modelTetrad->plugin = &standIn;
    plugin::plugins.push_back(&standIn);
    int failures = 0;
    for (int k = 0; k < n; ++k) {
        json_error_t err;
        json_t* root = json_load_file(paths[k], 0, &err);
        const char* base = std::strrchr(paths[k], '/');
        base = base ? base + 1 : paths[k];
        if (!root) {
            std::printf("F %s unreadable\n", base);
            ++failures;
            continue;
        }
        json_t* modules = json_object_get(root, "modules");
        if (modules) {
            size_t i;
            json_t* mj;
            int count = 0;
            json_array_foreach(modules, i, mj)
            {
                const char* model = json_string_value(json_object_get(mj, "model"));
                const char* plug = json_string_value(json_object_get(mj, "plugin"));
                if (model && plug && !std::strcmp(plug, "Fourfold") && !std::strcmp(model, "Tetrad")) {
                    char label[64];
                    std::snprintf(label, sizeof label, "%s#%d", base, count++);
                    failures += playFixture(label, mj);
                }
            }
        } else {
            failures += playFixture(base, root);
        }
        json_decref(root);
    }
    plugin::plugins.pop_back();
    modelTetrad->plugin = nullptr;
    standIn.models.clear();
    return failures;
}

// Tetrad and Tetrad Pipe side by side, as in a patch: the Pipe reads Tetrad's frame one sample
// late through Rack's expander messages, flipped here after each sample as the engine does. Both
// modules' outputs go into the hash. `routes`: the Pipe's saved routes (none: its defaults).
template <typename F>
void pipeScenario(const char* name, const char* routes, float seconds, F&& before)
{
    auto m = openPatch();
    std::unique_ptr<Module> pipe(modelTetradPipe->createModule());
    if (routes) {
        json_error_t err;
        json_t* j = json_loads(routes, 0, &err);
        pipe->dataFromJson(j);
        json_decref(j);
    }
    for (auto& o : pipe->outputs)
        o.channels = 1;
    m->rightExpander.module = pipe.get();
    pipe->leftExpander.module = m.get();
    int64_t frame = 0;
    std::vector<bool> wasGate(pipe->outputs.size(), false);
    auto both = [&](Hash& h) {
        step(*m, frame, h);
        Module::ProcessArgs a{kRate, 1.f / kRate, frame};
        pipe->process(a);
        if (pipe->leftExpander.messageFlipRequested) {
            std::swap(pipe->leftExpander.producerMessage, pipe->leftExpander.consumerMessage);
            pipe->leftExpander.messageFlipRequested = false;
        }
        for (size_t n = 0; n < pipe->outputs.size(); ++n) {
            auto& o = pipe->outputs[n];
            const int c = o.getChannels();
            h.add(&c, sizeof c);
            for (int k = 0; k < c; ++k) {
                const float v = o.getVoltage(k);
                h.add(&v, sizeof v);
            }
            const bool gate = c > 0 && o.getVoltage(0) >= 9.99f;
            h.pipeRises += gate && !wasGate[n];
            wasGate[n] = gate;
        }
    };
    {
        Hash unused;   // the opening, PLAY off (as the warm-up of phases 1-6)
        unused.pipeRises = 0;
        m->params[Tetrad::PLAY_PARAM].setValue(0.f);
        for (int i = 0; i < static_cast<int>(0.01f * kRate); ++i)
            both(unused);
        m->params[Tetrad::PLAY_PARAM].setValue(1.f);
    }
    Hash h;
    h.pipeRises = 0;
    for (int i = 0; i < static_cast<int>(seconds * kRate); ++i) {
        before(*m, i);
        both(h);
    }
    // The Pipe's saved routes, as a patch keeps them, into the hash too.
    json_t* saved = pipe->dataToJson();
    char* text = json_dumps(saved, JSON_SORT_KEYS);
    h.add(text, std::strlen(text));
    std::free(text);
    json_decref(saved);
    gTotal.add(&h.h, sizeof h.h);
    print(name, h);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && !std::strcmp(argv[1], "fixture")) {
        if (argc == 2) {
            std::fprintf(stderr, "usage: render_dump fixture FILE...\n");
            return 2;
        }
        return playFixtures(argc - 2, argv + 2);
    }
    auto mp = openPatch();
    Tetrad& m = *mp;
    int64_t frame = 0;

    // Warm-up, PLAY off, outside the TOTAL: the phases compare what follows the opening (2.7.1
    // runs its first control pass on the first sample, 2.7.0 on the 32nd; see phase 0 below).
    {
        Hash unused;
        m.params[Tetrad::PLAY_PARAM].setValue(0.f);
        for (int i = 0; i < static_cast<int>(0.01f * kRate); ++i)
            step(m, frame, unused);
        m.params[Tetrad::PLAY_PARAM].setValue(1.f);
    }
    phase(m, frame, "1 internal clock, RUN unpatched", 3.f, true, [](int) {});
    phase(m, frame, "2 RUN gate 5 V / 0 V every 0.4 s", 3.f, true,
          [&](int i) { connect(m, Tetrad::RUN_INPUT, (i / 19200) % 2 ? 0.f : 5.f); });
    phase(m, frame, "3 RUN at the threshold (1 V/0.99)", 2.f, true,
          [&](int i) { connect(m, Tetrad::RUN_INPUT, (i / 9600) % 2 ? 0.99f : 1.f); });
    phase(m, frame, "4 RUN NaN, then 10 V", 1.f, true,
          [&](int i) { connect(m, Tetrad::RUN_INPUT, i < 24000 ? NAN : 10.f); });
    phase(m, frame, "5 external clock 120 BPM, RESET", 4.f, true, [&](int i) {
        connect(m, Tetrad::CLOCK_INPUT, i % kBeat < 48 ? 10.f : 0.f);   // 1 PPQN, 1 ms pulses
        connect(m, Tetrad::RESET_INPUT, i >= 96000 && i < 96048 ? 10.f : 0.f);
        connect(m, Tetrad::RUN_INPUT, i >= 48000 && i < 96000 ? 0.f : 10.f);   // paused for a second
    });
    phase(m, frame, "6 PLAY off then on, RUN unplugged", 2.f, true, [&](int i) {
        if (i == 0) {
            unplug(m, Tetrad::CLOCK_INPUT);
            unplug(m, Tetrad::RESET_INPUT);
            unplug(m, Tetrad::RUN_INPUT);
        }
        m.params[Tetrad::PLAY_PARAM].setValue(i >= 9600 && i < 19200 ? 0.f : 1.f);
    });

    // Phases 8-13, each on a fresh module, in the TOTAL: an external clock that is irregular but
    // never stops the transport (no gap goes beyond four periods). 2.7.0 took each gap as the period;
    // so must 2.7.1.
    clockScenario("8 clock swing 17000 / 7000", 6.f, [](int k) { return k % 2 ? 7000 : 17000; });
    clockScenario("9 clock slows down 24000 -> 60000", 8.f, [](int k) { return k < 4 ? kBeat : 60000; });
    clockScenario("10 clock misses two pulses", 6.f, [](int k) { return k == 3 ? 3 * kBeat : kBeat; });
    clockScenario("11 clock stops 1.2 s (brief)", 6.f, [](int k) { return k == 3 ? kBeat + 57600 : kBeat; });
    // Exactly four periods: still not a stop (the transport stops beyond four periods).
    clockScenario("12 clock misses three pulses (4 periods)", 8.f, [](int k) { return k == 3 ? 4 * kBeat : kBeat; });
    clockScenario("13 clock slows down x4 24000 -> 96000", 10.f, [](int k) { return k < 4 ? kBeat : 4 * kBeat; });
    // Phase 16, in the TOTAL, on a fresh module: settings moved during playback, as MIDI-Map or
    // an automation does (the gate length and velocity of track 1 every 1000 samples, the swing of
    // track 2 every 1700): applied by the same control passes as in 2.7.0, on the same samples.
    {
        auto a = openPatch();
        int64_t f = 0;
        Hash unused;   // the opening, PLAY off (as the warm-up of phases 1-6)
        a->params[Tetrad::PLAY_PARAM].setValue(0.f);
        for (int i = 0; i < static_cast<int>(0.01f * kRate); ++i)
            step(*a, f, unused);
        a->params[Tetrad::PLAY_PARAM].setValue(1.f);
        phase(*a, f, "16 settings moved during playback", 4.f, true, [&](int i) {
            if (i % 1000 == 0) {
                a->params[trackParamIndex(0, kTrackGate)].setValue(static_cast<float>(20 + (i / 1000) % 70));
                a->params[trackParamIndex(0, kTrackVelocity)].setValue(static_cast<float>(40 + (i / 1000) % 80));
            }
            if (i % 1700 == 0)
                a->params[trackParamIndex(1, kTrackSwing)].setValue(static_cast<float>(50 + (i / 1700) % 25));
        });
    }
    // Phases 14-15, in the TOTAL: Tetrad Pipe, its default routes (pitch, velocity and gate of the
    // four tracks, triggers, clock and reset) over an external clock with a RESET, then routes to
    // the drum rows of track 4 (signals by their saved number: 5 row gate, 6 row trigger, 7 row
    // velocity).
    pipeScenario("14 Tetrad Pipe, default routes", nullptr, 4.f, [](Tetrad& t, int i) {
        connect(t, Tetrad::CLOCK_INPUT, i % kBeat < 48 ? 10.f : 0.f);
        connect(t, Tetrad::RESET_INPUT, i >= 96000 && i < 96048 ? 10.f : 0.f);
    });
    {
        std::string routes = "{\"routes\": [";
        for (int i = 0; i < 16; ++i) {
            const int signal = i < 8 ? 7 : i < 14 ? 5 : 6;
            routes += (i ? ", " : "") + std::string("{\"signal\": ") + std::to_string(signal) +
                      ", \"track\": 3, \"row\": " + std::to_string(i % 8) + "}";
        }
        routes += "]}";
        pipeScenario("15 Tetrad Pipe, drum rows of track 4", routes.c_str(), 4.f, [](Tetrad&, int) {});
    }
    std::printf("%-40s %016llx\n", "TOTAL", static_cast<unsigned long long>(gTotal.h));

    // Phase 7, apart from the TOTAL (may differ): the external clock stops for 2 s, a stop of the
    // transport, then starts again. 2.7.1 keeps the last tempo across the stop (2.7.0 took the stop
    // as the period).
    phase(m, frame, "7 clock stop 2 s, restart (may differ)", 6.f, false, [&](int i) {
        const bool stopped = i >= 72000 && i < 168000;   // 1.5 s .. 3.5 s
        connect(m, Tetrad::CLOCK_INPUT, !stopped && i % kBeat < 48 ? 10.f : 0.f);
        connect(m, Tetrad::RUN_INPUT, 10.f);
    });

    // Phase 0: a patch opens while its clock strikes on the very first sample. Its notes are set
    // before the first sample (as a patch's are, loaded before the engine runs it). 0a, the first
    // beat, may differ (2.7.1 plays step 1, 2.7.0 left it silent); 0b, everything after it, must
    // be identical.
    {
        auto o = openPatch();
        int64_t f = 0;
        Hash a, b;
        static constexpr int kStarts[] = {1, 1 + kBeat, 1 + 2 * kBeat, 1 + 3 * kBeat, 1 + 4 * kBeat,
                                          1 + 5 * kBeat, 1 + 6 * kBeat, 1 + 7 * kBeat};
        for (int i = 0; i < 8 * kBeat; ++i) {
            // As through a cable: 0 V on the first sample, the clock's first pulse on the second.
            connect(*o, Tetrad::CLOCK_INPUT, pulseAt(kStarts, 8, i) ? 10.f : 0.f);
            step(*o, f, i <= kBeat ? a : b);   // the second pulse is on sample 1 + kBeat
        }
        print("0a opening, first beat (may differ)", a);
        print("0b opening, after the first beat", b);
    }

    PatternBank bank;
    m.copyPatterns(bank);
    int active = 0;
    for (int t = 0; t < kNumTracks; ++t)
        for (int i = 0; i < kMaxSteps; ++i)
            active += bank.tracks[t].steps[i].active || bank.tracks[t].steps[i].hits;
    std::printf("steps written by the generator: %d\n", active);
    return 0;
}

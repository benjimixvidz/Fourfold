#include "Tetrad.hpp"

#include "../core/state.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr int kControlRate = 32;   // samples between parameter and pattern exchange checks
constexpr float kTriggerSeconds = 1e-3f;

bool randomizable(int index)
{
    if (index < kNumGlobalParams)
        return false;
    const int p = (index - kNumGlobalParams) % kNumTrackParams;
    return p == kTrackSeed || p == kTrackDensity || p == kTrackRange || p == kTrackRatchet || p == kTrackVariation;
}

// Saved data: `root[key]` as an index into `count` choices, an integer (or a whole real), clamped.
// Anything else, or no such key (older patches), leaves `out` as it is.
void readIndex(json_t* root, const char* key, int count, std::atomic<int>& out)
{
    json_t* p = json_object_get(root, key);
    if (!p)
        return;
    double index = NAN;
    if (json_is_integer(p))
        index = double(json_integer_value(p));
    else if (json_is_real(p) && std::floor(json_real_value(p)) == json_real_value(p))
        index = json_real_value(p);
    if (!std::isnan(index))
        out = int(std::clamp(index, 0.0, double(count - 1)));
}

} // namespace

Tetrad::Tetrad()
{
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
    const ParamDesc* table = paramTable();
    for (int i = 0; i < kNumParams; ++i) {
        const ParamDesc& d = table[i];
        ParamQuantity* q;
        if (d.labels) {
            std::vector<std::string> labels;
            for (int k = 0; d.labels[k]; ++k)
                labels.push_back(d.labels[k]);
            q = configSwitch(i, float(d.min), float(d.max), float(d.def), d.name, labels);
        } else {
            q = configParam(i, float(d.min), float(d.max), float(d.def), d.name, d.suffix);
            q->snapEnabled = true;
        }
        // Randomize only touches the generator, never the track layout.
        q->randomizeEnabled = randomizable(i);
        q->resetEnabled = d.type != ParamType::Reserved;
    }
    configParam(TEMPO_PARAM, 20.f, 300.f, 120.f, "Internal tempo", " BPM");
    configSwitch(PLAY_PARAM, 0.f, 1.f, 1.f, "Play", {"Stopped", "Playing"});
    configButton(RESET_PARAM, "Reset (back to step 1)");
    ParamQuantity* expert =
        configSwitch(EXPERT_PARAM, 0.f, 1.f, 0.f, "Expert view", {"Off: the main settings", "On: every setting"});
    expert->randomizeEnabled = false;
    expert->resetEnabled = false;
    configInput(CLOCK_INPUT, "Clock (unpatched: internal clock)");
    configInput(RESET_INPUT, "Reset");
    configInput(RUN_INPUT, "Run (trigger or gate: see the right-click menu; unpatched = always)");
    configInput(TRANSPOSE_INPUT, "Transpose (1 V/oct, rounded to semitones)");
    for (int t = 0; t < kNumTracks; ++t) {
        configOutput(VOCT_OUTPUT + t, string::f("Track %d V/oct", t + 1));
        configOutput(GATE_OUTPUT + t, string::f("Track %d gate", t + 1));
        configOutput(VEL_OUTPUT + t, string::f("Track %d velocity (0-10 V)", t + 1));
        configLight(ON_LIGHT + t, string::f("Track %d on", t + 1));
        playheadSteps[t].store(0);
    }
    configLight(CLOCK_LIGHT, "Clock");

    for (int i = 0; i < kNumParams; ++i)
        cachedValues[i] = params[i].getValue();
    applySettings();
    // A new Tetrad starts empty: the generator only writes once one of its settings moves
    // (SEED, NEW SEED, DENS...).
    seq.setPatterns(emptyBank());
    sharedBank = seq.patterns();
    publishedVersion = seq.patternVersion();
}

PatternBank Tetrad::emptyBank()
{
    double values[kNumParams];
    for (int i = 0; i < kNumParams; ++i)
        values[i] = params[i].getValue();
    const Settings s = settingsFromValues(values);
    PatternBank bank;
    for (int t = 0; t < kNumTracks; ++t)
        bank.tracks[t].key = s.tracks[t].gen;
    return bank;
}

// A patch (its module carries an "id") restores the view and the RUN trigger mode's play / pause;
// a preset or a paste keeps them (a preset loaded while the clock runs must not pause Tetrad).
void Tetrad::fromJson(json_t* root)
{
    const float expert = params[EXPERT_PARAM].getValue();
    const bool latch = runLatch.load();
    const int mode = runMode.load();
    Module::fromJson(root);
    if (!json_object_get(root, "id")) {
        params[EXPERT_PARAM].setValue(expert);
        runLatch = latch;
        // A preset or a paste from 2.7.0 or earlier has no run mode: the module keeps its own,
        // set for the cable on RUN (Gate is for patches made with 2.7.0).
        if (!json_object_get(json_object_get(root, "data"), "runMode"))
            runMode = mode;
    }
}

void Tetrad::setRunMode(int mode)
{
    mode = std::clamp(mode, 0, kNumRunModes - 1);
    if (mode != runMode.load())
        runLatch = true;
    runMode = mode;
}

namespace {

// A RUN mode chosen in the menu, as an undo entry: the mode and the play / pause state with it.
struct RunModeChange : history::ModuleAction {
    int before = 0, after = 0;
    bool latchBefore = true, latchAfter = true;
    void set(int mode, bool latch)
    {
        if (auto* m = dynamic_cast<Tetrad*>(APP->engine->getModule(moduleId))) {
            m->setRunMode(mode);
            m->runLatch = latch;
        }
    }
    void undo() override { set(before, latchBefore); }
    void redo() override { set(after, latchAfter); }
};

} // namespace

void Tetrad::changeRunMode(int mode)
{
    const int before = runMode.load();
    if (mode == before)
        return;
    const bool latchBefore = runLatch.load();
    setRunMode(mode);
    auto a = std::make_unique<RunModeChange>();
    a->moduleId = id;
    a->name = "change RUN input mode";
    a->before = before;
    a->after = mode;
    a->latchBefore = latchBefore;
    a->latchAfter = runLatch.load();
    APP->history->push(a.release());   // the history owns it from here
}

// Also called when the module is added to the engine.
void Tetrad::onSampleRateChange(const SampleRateChangeEvent& e)
{
    seq.reset(e.sampleRate);
    voices.clear();
    voices.setRetrigSamples(int(e.sampleRate * kTriggerSeconds));
}

void Tetrad::onReset(const ResetEvent& e)
{
    Module::onReset(e);
    ppqnIndex = 0;
    runMode = kRunTrigger;   // as a new module
    runLatch = true;
    // Initialize empties every track, as a new module.
    tetrad::SpinGuard lock(bankMutex);
    sharedBank = emptyBank();
    publishEdit();
}

void Tetrad::publishEdit()
{
    loadPending = true;
    bankSerial.fetch_add(1, std::memory_order_release);
}

void Tetrad::replacePatterns(const PatternBank& bank)
{
    tetrad::SpinGuard lock(bankMutex);
    sharedBank = bank;
    publishEdit();
}

void Tetrad::copyPatterns(PatternBank& out)
{
    tetrad::SpinGuard lock(bankMutex);
    out = sharedBank;
}

void Tetrad::applySettings()
{
    seq.setSettings(settingsFromValues(cachedValues));
    for (int t = 0; t < kNumTracks; ++t)
        voices.setMode(t, seq.settings().tracks[t].mode);
}

// Every kControlRate samples: parameters, pattern exchange with the UI, lights.
void Tetrad::control(float sampleTime)
{
    bool changed = false;
    for (int i = 0; i < kNumParams; ++i) {
        const double v = params[i].getValue();
        if (v != cachedValues[i]) {
            cachedValues[i] = v;
            changed = true;
        }
    }

    const bool locked = bankMutex.try_lock();   // the engine thread never waits for the UI here
    if (locked && loadPending) {
        seq.setPatterns(sharedBank);
        loadPending = false;
        changed = true;
    }
    if (changed)
        applySettings();
    // The generator may have rewritten a track: publish it to the UI.
    if (locked && seq.patternVersion() != publishedVersion) {
        sharedBank = seq.patterns();
        publishedVersion = seq.patternVersion();
        bankSerial.fetch_add(1, std::memory_order_release);
    }
    if (locked)
        bankMutex.unlock();

    for (int t = 0; t < kNumTracks; ++t) {
        playheadSteps[t].store(seq.playhead(t), std::memory_order_relaxed);
        lights[ON_LIGHT + t].setBrightness(params[trackParamIndex(t, kTrackEnabled)].getValue() > 0.5f);
    }
    lights[CLOCK_LIGHT].setBrightness(clockLight.process(sampleTime * kControlRate) ? 1.f : 0.f);
}

void Tetrad::process(const ProcessArgs& args)
{
    // Every kControlRate samples, as in 2.7.0, plus one pass on the very first sample: the
    // patterns and settings of a patch that opens are in place for its first beat (a clock
    // striking on the first sample used to find them empty).
    const bool first = firstControl;
    firstControl = false;
    if (++controlCounter >= kControlRate || first) {
        if (controlCounter >= kControlRate)
            controlCounter = 0;
        control(args.sampleTime);
    }

    // Transport: CLOCK patched = external clock, otherwise the internal one at TEMPO.
    // PLAY off stops and goes back to step 1; RUN (gate low, or toggled off) pauses.
    const bool external = inputs[CLOCK_INPUT].isConnected();
    externalClock.store(external, std::memory_order_relaxed);
    const bool playOn = params[PLAY_PARAM].getValue() > 0.5f;
    const bool resetPressed = resetButton.process(params[RESET_PARAM].getValue() > 0.5f);
    const bool resetEdge = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f);
    if (resetEdge || resetPressed || (wasPlayOn && !playOn)) {
        clockHoldoff = 1e-3f;   // VCV: ignore CLOCK for 1 ms after RESET
        clock.reset();
        seq.rewind();
        internalBeat = 0.0;
        internalTick = -1;
        if (resetEdge || resetPressed)
            resetOut.trigger(kTriggerSeconds);
    }
    wasPlayOn = playOn;
    // RUN: unpatched, it follows PLAY. Gate mode: high (1 V and up) plays. Trigger mode: each
    // rising edge toggles play and pause. The edge detector runs in both modes, so a switch to
    // trigger mode while the input is high is not an edge; nor are the first samples (runGuard).
    bool runEdge = runTrigger.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f);
    if (runGuard > 0) {
        --runGuard;
        runEdge = false;
    }
    bool runHigh = true;
    if (inputs[RUN_INPUT].isConnected()) {
        if (runMode.load(std::memory_order_relaxed) == kRunTrigger) {
            if (runEdge) {
                const bool start = !runLatch.load(std::memory_order_relaxed);
                runLatch.store(start, std::memory_order_relaxed);
                // A start over an external clock waits for its next pulse. A clock held high while
                // stopped (Impromptu Clocked) gives no edge on its first beat, and an edge Tetrad
                // saw before the start (a patch opening over that high clock reads 0 V first) must
                // not make it play a step on its own, off the beat.
                awaitPulse = start && external;
                // The gap up to that pulse holds the pause, not a period of the clock. A start
                // that comes just after an edge (RUN through one more module than CLOCK) finds
                // that gap already counted: the next one is a period and is measured.
                if (start && external && clockEdgeAge >= int(args.sampleRate * kTriggerSeconds))
                    clock.skipNextGap();
            }
            runHigh = runLatch.load(std::memory_order_relaxed);
        } else
            runHigh = inputs[RUN_INPUT].getVoltage() >= 1.f;
    }
    if (!external || !runHigh)
        awaitPulse = false;
    const bool run = playOn && runHigh;
    const int ppqn = kPpqnChoices[ppqnIndex.load(std::memory_order_relaxed)];
    TransportState tr;
    bool tick = false;
    if (external) {
        tick = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f) && clockHoldoff <= 0.f;
        clockHoldoff -= args.sampleTime;
        clockEdgeAge = tick ? 0 : std::min(clockEdgeAge + 1, 1 << 30);
        if (tick)
            awaitPulse = false;
        clock.setPpqn(ppqn);
        tr = clock.tick(tick, run && !awaitPulse, args.sampleRate);
    } else {
        tr.tempo = clamp(params[TEMPO_PARAM].getValue(), 20.f, 300.f);
        tr.beat = internalBeat;
        tr.playing = run;
        if (run) {
            const int64_t t = int64_t(std::floor(internalBeat * ppqn + 1e-9));
            tick = t != internalTick;
            internalTick = t;
            internalBeat += tr.tempo / 60.0 * args.sampleTime;
        }
    }
    if (tick) {
        clockLight.trigger(0.02f);
        clockOut.trigger(kTriggerSeconds);
    }
    running.store(tr.playing, std::memory_order_relaxed);
    if (tr.playing && !wasRunning)
        resetOut.trigger(kTriggerSeconds);
    wasRunning = tr.playing;
    if (tr.playing || !external)
        tempoNow.store(tr.tempo, std::memory_order_relaxed);

    const int semis = inputs[TRANSPOSE_INPUT].isConnected()
                          ? int(std::round(clamp(inputs[TRANSPOSE_INPUT].getVoltage(), -4.f, 4.f) * 12.f))
                          : 0;
    if (semis != transposeSemis) {
        seq.setTranspose(semis);
        transposeSemis = semis;
    }

    voices.beginSample();
    seq.render(tr, 0, 1, voices);
    voices.output(frame);

    for (int t = 0; t < kNumTracks; ++t) {
        Output& voct = outputs[VOCT_OUTPUT + t];
        Output& gate = outputs[GATE_OUTPUT + t];
        Output& vel = outputs[VEL_OUTPUT + t];
        const int n = frame.channels[t];
        for (int c = 0; c < n; ++c) {
            voct.setVoltage((frame.key[t][c] - 60) / 12.f, c);
            gate.setVoltage((frame.gateMask[t] >> c) & 1u ? 10.f : 0.f, c);
            vel.setVoltage(frame.velocity[t][c] * (10.f / 127.f), c);
        }
        voct.setChannels(n);
        gate.setChannels(n);
        vel.setChannels(n);
    }

    const bool clockHigh = clockOut.process(args.sampleTime);
    const bool resetHigh = resetOut.process(args.sampleTime);
    Module* right = rightExpander.module;
    if (right && right->model == modelTetradPipe) {
        auto* msg = static_cast<PipeMessage*>(right->leftExpander.producerMessage);
        msg->frame = frame;
        msg->serial = ++pipeSerial;
        msg->clock = clockHigh;
        msg->reset = resetHigh;
        right->leftExpander.messageFlipRequested = true;
    }
}

json_t* Tetrad::dataToJson()
{
    PatternBank bank;
    copyPatterns(bank);
    json_t* root = json_object();
    json_object_set_new(root, "ppqn", json_integer(ppqnIndex.load()));
    json_object_set_new(root, "runMode", json_integer(runMode.load()));
    json_object_set_new(root, "runLatch", json_boolean(runLatch.load()));
    json_object_set_new(root, "state", json_string(serializePatterns(bank).c_str()));
    return root;
}

void Tetrad::dataFromJson(json_t* root)
{
    if (json_t* p = json_object_get(root, "ppqn"))
        ppqnIndex = clamp(int(json_integer_value(p)), 0, kNumPpqnChoices - 1);
    // Patches and presets from 2.7.0 and earlier have no run mode: they keep the gate they were
    // made with, so they play as before. A new Tetrad (no data loaded) uses triggers.
    if (!json_object_get(root, "runMode"))
        runMode = kRunGate;
    else
        readIndex(root, "runMode", kNumRunModes, runMode);
    if (json_t* l = json_object_get(root, "runLatch"); json_is_boolean(l))
        runLatch = json_is_true(l);
    json_t* st = json_object_get(root, "state");
    PatternBank bank;
    if (!json_is_string(st) || !deserializePatterns(json_string_value(st), bank))
        return;
    replacePatterns(bank);
}

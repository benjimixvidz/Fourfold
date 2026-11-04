#include "Tetrad.hpp"

#include "../core/state.h"

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
    configInput(CLOCK_INPUT, "Clock (unpatched: internal clock)");
    configInput(RESET_INPUT, "Reset");
    configInput(RUN_INPUT, "Run (high = play, unpatched = always)");
    configInput(TRANSPOSE_INPUT, "Transpose (1 V/oct, rounded to semitones)");
    for (int t = 0; t < kNumTracks; ++t) {
        configOutput(VOCT_OUTPUT + t, string::f("Track %d V/oct", t + 1));
        configOutput(GATE_OUTPUT + t, string::f("Track %d gate", t + 1));
        configOutput(VEL_OUTPUT + t, string::f("Track %d velocity (0-10 V)", t + 1));
        configLight(ON_LIGHT + t, string::f("Track %d on", t + 1));
        playheadSteps[t].store(0);
    }
    configLight(CLOCK_LIGHT, "Clock");
    configLight(PLAY_LIGHT, "Playing");

    for (int i = 0; i < kNumParams; ++i)
        cachedValues[i] = params[i].getValue();
    applySettings();
    sharedBank = seq.patterns();
    publishedVersion = seq.patternVersion();
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
    // Default generator keys make every track regenerate, hand edits included.
    std::lock_guard<std::mutex> lock(bankMutex);
    sharedBank = PatternBank{};
    publishEdit();
}

void Tetrad::publishEdit()
{
    loadPending = true;
    bankSerial.fetch_add(1, std::memory_order_release);
}

void Tetrad::replacePatterns(const PatternBank& bank)
{
    std::lock_guard<std::mutex> lock(bankMutex);
    sharedBank = bank;
    publishEdit();
}

void Tetrad::copyPatterns(PatternBank& out)
{
    std::lock_guard<std::mutex> lock(bankMutex);
    out = sharedBank;
}

void Tetrad::applySettings()
{
    seq.setSettings(settingsFromValues(cachedValues));
    for (int t = 0; t < kNumTracks; ++t)
        voices.setPoly(t, seq.settings().tracks[t].mode == kModePoly);
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

    std::unique_lock<std::mutex> lock(bankMutex, std::try_to_lock);
    if (lock.owns_lock() && loadPending) {
        seq.setPatterns(sharedBank);
        loadPending = false;
        changed = true;
    }
    if (changed)
        applySettings();
    // The generator may have rewritten a track: publish it to the UI.
    if (lock.owns_lock() && seq.patternVersion() != publishedVersion) {
        sharedBank = seq.patterns();
        publishedVersion = seq.patternVersion();
        bankSerial.fetch_add(1, std::memory_order_release);
    }
    if (lock.owns_lock())
        lock.unlock();

    for (int t = 0; t < kNumTracks; ++t) {
        playheadSteps[t].store(seq.playhead(t), std::memory_order_relaxed);
        lights[ON_LIGHT + t].setBrightness(params[trackParamIndex(t, kTrackEnabled)].getValue() > 0.5f);
    }
    lights[CLOCK_LIGHT].setBrightness(clockLight.process(sampleTime * kControlRate) ? 1.f : 0.f);
    lights[PLAY_LIGHT].setBrightness(params[PLAY_PARAM].getValue() > 0.5f ? 1.f : 0.f);
}

void Tetrad::process(const ProcessArgs& args)
{
    if (++controlCounter >= kControlRate) {
        controlCounter = 0;
        control(args.sampleTime);
    }

    // Transport: CLOCK patched = external clock, otherwise the internal one at TEMPO.
    // PLAY off stops and goes back to step 1; RUN low pauses.
    const bool external = inputs[CLOCK_INPUT].isConnected();
    externalClock.store(external, std::memory_order_relaxed);
    const bool playOn = params[PLAY_PARAM].getValue() > 0.5f;
    const bool resetPressed = resetButton.process(params[RESET_PARAM].getValue() > 0.5f);
    const bool resetEdge = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f);
    if (resetEdge || resetPressed || (wasPlayOn && !playOn)) {
        clock.reset();
        internalBeat = 0.0;
        internalTick = -1;
        if (resetEdge || resetPressed)
            resetOut.trigger(kTriggerSeconds);
    }
    wasPlayOn = playOn;
    const bool run = playOn && (!inputs[RUN_INPUT].isConnected() || inputs[RUN_INPUT].getVoltage() >= 1.f);
    const int ppqn = kPpqnChoices[ppqnIndex.load(std::memory_order_relaxed)];
    TransportState tr;
    bool tick = false;
    if (external) {
        tick = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f);
        clock.setPpqn(ppqn);
        tr = clock.tick(tick, run, args.sampleRate);
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
    json_object_set_new(root, "state", json_string(serializePatterns(bank).c_str()));
    return root;
}

void Tetrad::dataFromJson(json_t* root)
{
    if (json_t* p = json_object_get(root, "ppqn"))
        ppqnIndex = clamp(int(json_integer_value(p)), 0, kNumPpqnChoices - 1);
    json_t* st = json_object_get(root, "state");
    PatternBank bank;
    if (!json_is_string(st) || !deserializePatterns(json_string_value(st), bank))
        return;
    replacePatterns(bank);
}

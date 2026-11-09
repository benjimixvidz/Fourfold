#pragma once
// Tetrad module, engine side. Threads: process() runs on the engine thread; the UI edits patterns
// through `sharedBank` (a spinlock, try_lock only on the engine side) and reads the playheads,
// tempo and transport state through atomics.

#include "plugin.hpp"
#include "pipe.hpp"
#include "spin.hpp"

#include "../core/cv.h"
#include "../core/params.h"
#include "../core/sequencer.h"

#include <atomic>

using namespace rack;
using namespace tetrad;

inline constexpr int kPpqnChoices[] = {1, 2, 4, 8, 12, 24, 48};
inline constexpr int kNumPpqnChoices = int(sizeof(kPpqnChoices) / sizeof(int));

struct Tetrad : Module {
    enum InputId { CLOCK_INPUT, RESET_INPUT, RUN_INPUT, TRANSPOSE_INPUT, NUM_INPUTS };
    enum OutputId { ENUMS(VOCT_OUTPUT, kNumTracks), ENUMS(GATE_OUTPUT, kNumTracks), ENUMS(VEL_OUTPUT, kNumTracks), NUM_OUTPUTS };
    enum LightId { ENUMS(ON_LIGHT, kNumTracks), CLOCK_LIGHT, NUM_LIGHTS };
    // Parameter ids 0..kNumParams-1 are the core table; these come after it.
    // EXPERT is the panel's view (the inspector's main settings or every setting), not a setting
    // of the sequence: presets and Initialize leave it as it is.
    enum ExtraParam { TEMPO_PARAM = kNumParams, PLAY_PARAM, RESET_PARAM, EXPERT_PARAM, NUM_PARAMS };

    // RUN input: a trigger (each one toggles play and pause, as sent by Impromptu Clocked or
    // VCV SEQ3 on start and on stop) or a gate (high plays, low pauses). The values are saved in
    // patches: never renumber them.
    enum RunMode { kRunGate, kRunTrigger, kNumRunModes };

    // UI-visible state.
    std::atomic<int> ppqnIndex{0};   // 1 PPQN: one pulse per quarter note, like most clock modules
    // A new Tetrad uses triggers; a patch from 2.7.0 or earlier (no saved run mode) keeps the
    // gate it was made with (dataFromJson).
    std::atomic<int> runMode{kRunTrigger};
    // Trigger mode: playing or paused, saved with the patch. It starts playing, as Clocked and
    // most clock modules do by default, so that their first pulse (stop) pauses Tetrad too.
    std::atomic<bool> runLatch{true};
    std::atomic<bool> externalClock{false};
    std::atomic<bool> running{false};
    std::atomic<double> tempoNow{0.0};
    std::atomic<int> playheadSteps[kNumTracks];
    std::atomic<uint32_t> bankSerial{0};   // bumped whenever sharedBank changes

    Tetrad();
    void onSampleRateChange(const SampleRateChangeEvent& e) override;
    void onReset(const ResetEvent& e) override;
    void process(const ProcessArgs& args) override;
    json_t* dataToJson() override;
    void dataFromJson(json_t* root) override;
    void fromJson(json_t* root) override;
    // UI thread (menu): a new RUN mode starts playing (see runLatch), whatever trigger mode was
    // left at before.
    void setRunMode(int mode);
    // The menu's choice (UI thread): applied, and one undo entry in Rack's history.
    void changeRunMode(int mode);

    // UI thread: applies `fn` to the shared patterns; returns true if it changed them.
    template <typename F>
    bool editPatterns(F&& fn)
    {
        tetrad::SpinGuard lock(bankMutex);
        if (!fn(sharedBank))
            return false;
        publishEdit();
        return true;
    }
    void replacePatterns(const PatternBank& bank);
    void copyPatterns(PatternBank& out);

private:
    Sequencer seq;
    CvVoices voices;
    double cachedValues[kNumParams];
    CvFrame frame{};

    ClockFollower clock;
    dsp::SchmittTrigger clockTrigger, resetTrigger, runTrigger;
    float clockHoldoff = 0.f;   // seconds left during which CLOCK edges are ignored
    // RUN edges are ignored for the first samples of the module: Rack's cables deliver an output
    // one sample late (0 V first), so a source already high when a patch loads would otherwise
    // look like a rising edge and toggle trigger mode.
    static constexpr int kRunGuardSamples = 20;
    int runGuard = kRunGuardSamples;   // engine thread only
    bool awaitPulse = false;   // engine thread: a trigger-mode start waits for the next CLOCK pulse
    dsp::BooleanTrigger resetButton;
    dsp::PulseGenerator clockLight, clockOut, resetOut;
    bool wasRunning = false;
    bool wasPlayOn = true;
    double internalBeat = 0.0;   // quarter notes
    int64_t internalTick = -1;   // last internal clock tick sent to the clock output
    int transposeSemis = 0;
    int controlCounter = 0;
    bool firstControl = true;   // one control pass on the very first sample
    int clockEdgeAge = 1 << 30;  // samples since the last counted clock edge
    uint32_t pipeSerial = 0;     // stamps each message to the Pipe

    tetrad::SpinMutex bankMutex;   // the engine thread only ever try_lock()s it
    PatternBank sharedBank;      // guarded by bankMutex
    bool loadPending = false;    // guarded by bankMutex: sharedBank holds a UI edit
    uint64_t publishedVersion = 0;

    void publishEdit();          // with bankMutex held
    PatternBank emptyBank();     // no step on, keyed to the generator settings: nothing regenerates
    void control(float sampleTime);
    void applySettings();
};

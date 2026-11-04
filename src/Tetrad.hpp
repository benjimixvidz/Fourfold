#pragma once
// Tetrad module, engine side. Threads: process() runs on the engine thread; the UI edits patterns
// through `sharedBank` (mutex, try_lock only on the engine side) and reads the playheads, tempo
// and transport state through atomics.

#include "plugin.hpp"
#include "pipe.hpp"

#include "../core/cv.h"
#include "../core/params.h"
#include "../core/sequencer.h"

#include <atomic>
#include <mutex>

using namespace rack;
using namespace tetrad;

inline constexpr int kPpqnChoices[] = {1, 2, 4, 8, 12, 24, 48};
inline constexpr int kNumPpqnChoices = int(sizeof(kPpqnChoices) / sizeof(int));

struct Tetrad : Module {
    enum InputId { CLOCK_INPUT, RESET_INPUT, RUN_INPUT, TRANSPOSE_INPUT, NUM_INPUTS };
    enum OutputId { ENUMS(VOCT_OUTPUT, kNumTracks), ENUMS(GATE_OUTPUT, kNumTracks), ENUMS(VEL_OUTPUT, kNumTracks), NUM_OUTPUTS };
    enum LightId { ENUMS(ON_LIGHT, kNumTracks), CLOCK_LIGHT, PLAY_LIGHT, NUM_LIGHTS };
    // Parameter ids 0..kNumParams-1 are the core table; these come after it.
    enum ExtraParam { TEMPO_PARAM = kNumParams, PLAY_PARAM, RESET_PARAM, NUM_PARAMS };

    // UI-visible state.
    std::atomic<int> ppqnIndex{0};   // 1 PPQN: one pulse per quarter note, like most clock modules
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

    // UI thread: applies `fn` to the shared patterns; returns true if it changed them.
    template <typename F>
    bool editPatterns(F&& fn)
    {
        std::lock_guard<std::mutex> lock(bankMutex);
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
    dsp::SchmittTrigger clockTrigger, resetTrigger;
    dsp::BooleanTrigger resetButton;
    dsp::PulseGenerator clockLight, clockOut, resetOut;
    bool wasRunning = false;
    bool wasPlayOn = true;
    double internalBeat = 0.0;   // quarter notes
    int64_t internalTick = -1;   // last internal clock tick sent to the clock output
    int transposeSemis = 0;
    int controlCounter = 0;

    std::mutex bankMutex;
    PatternBank sharedBank;      // guarded by bankMutex
    bool loadPending = false;    // guarded by bankMutex: sharedBank holds a UI edit
    uint64_t publishedVersion = 0;

    void publishEdit();          // with bankMutex held
    void control(float sampleTime);
    void applySettings();
};

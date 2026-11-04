#pragma once
// Shared pieces of the Tetrad UI: colours, drawing helpers, the state shared by the inspector,
// the grid and the keyboard, undo helpers and the editing actions.

#include "Tetrad.hpp"

#include "../core/edit.h"
#include "../core/grid.h"
#include "../core/scales.h"

#include <memory>

namespace ui_tetrad {

using namespace rack;
using namespace tetrad;

// Text >= 4.5:1 and state >= 3:1 against the dark backgrounds (WCAG 1.4.3 / 1.4.11).
const NVGcolor kTrackColors[kNumTracks] = {nvgRGB(46, 196, 182), nvgRGB(244, 162, 89), nvgRGB(170, 120, 240),
                                           nvgRGB(241, 91, 181)};
const NVGcolor kScreen = nvgRGB(8, 10, 13);
const NVGcolor kField = nvgRGB(20, 23, 28);
const NVGcolor kPadOff = nvgRGB(44, 48, 57);
const NVGcolor kPadOutside = nvgRGB(28, 31, 37);
const NVGcolor kPadRoot = nvgRGB(66, 72, 88);
const NVGcolor kPlayhead = nvgRGB(96, 104, 120);
const NVGcolor kButton = nvgRGB(42, 46, 54);
const NVGcolor kButtonHover = nvgRGB(58, 63, 74);
const NVGcolor kButtonOn = nvgRGB(96, 104, 122);
const NVGcolor kText = nvgRGB(214, 220, 228);
const NVGcolor kTextDark = nvgRGB(14, 15, 18);
const NVGcolor kTextDim = nvgRGB(128, 136, 150);
const NVGcolor kTextOff = nvgRGB(60, 66, 76);
const NVGcolor kInk = nvgRGB(214, 236, 240);
const NVGcolor kSelect = nvgRGB(250, 250, 250);
const NVGcolor kMuted = nvgRGB(240, 112, 112);

inline void fillRect(NVGcontext* vg, const math::Rect& r, NVGcolor c, float radius = 2.f)
{
    nvgBeginPath(vg);
    nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, radius);
    nvgFillColor(vg, c);
    nvgFill(vg);
}

inline void strokeRect(NVGcontext* vg, const math::Rect& r, NVGcolor c, float width = 1.f, float radius = 2.f)
{
    nvgBeginPath(vg);
    nvgRoundedRect(vg, r.pos.x + width / 2, r.pos.y + width / 2, r.size.x - width, r.size.y - width, radius);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
}

inline void text(NVGcontext* vg, float x, float y, const std::string& s, NVGcolor c, float size,
                 int align = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE)
{
    nvgFontSize(vg, size);
    nvgFillColor(vg, c);
    nvgTextAlign(vg, align);
    nvgText(vg, x, y, s.c_str(), nullptr);
}

inline void centered(NVGcontext* vg, const math::Rect& r, const std::string& s, NVGcolor c, float size)
{
    text(vg, r.pos.x + r.size.x / 2, r.pos.y + r.size.y / 2 + 0.5f, s, c, size, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
}

inline bool setFont(NVGcontext* vg)
{
    auto f = panelFont();
    if (f)
        nvgFontFaceId(vg, f->handle);
    return bool(f);
}

// Parameter value, or its default without a module (module browser).
inline float paramValue(Module* m, int id)
{
    return m ? m->params[id].getValue() : id < kNumParams ? float(paramTable()[id].def) : 0.f;
}
inline int trackValue(Module* m, int track, int p)
{
    return int(std::round(paramValue(m, trackParamIndex(track, p))));
}
inline int loopStart(Module* m, int track) { return clamp(trackValue(m, track, kTrackStart) - 1, 0, kMaxSteps - 1); }
inline int loopLength(Module* m, int track) { return clamp(trackValue(m, track, kTrackLength), 1, kMaxSteps); }

// UI thread only.
struct UiState {
    GridView view;
    bool follow = true;
    bool help = false;
    int lowest[kNumTracks] = {0, 0, 0, 0};   // NOTE lane scroll per track
    StepMask selection;                      // steps of view.track
    int anchor = -1;                         // last clicked step, for Shift+click ranges
    Clipboard clipboard;
    int euclidPulses[kNumTracks] = {4, 4, 4, 4};
    int euclidRotation[kNumTracks] = {0, 0, 0, 0};
    std::string hint;                        // help line under the pointer

    PatternBank bank;                        // copy of the module's patterns, for drawing
    uint32_t serial = 0;
    bool haveBank = false;

    void refresh(Tetrad* m)
    {
        if (!m) {
            if (!haveBank)
                bank = Sequencer().patterns();   // module browser: the default patterns
            haveBank = true;
            return;
        }
        const uint32_t s = m->bankSerial.load(std::memory_order_acquire);
        if (!haveBank || s != serial) {
            m->copyPatterns(bank);
            serial = s;
            haveBank = true;
        }
    }

    void selectTrack(int t)
    {
        if (t == view.track)
            return;
        view.track = t;
        view.lowestDegree = lowest[t];
        selection.reset();
        anchor = -1;
    }
};

// Undo of a pattern edit: whole banks before and after (a few KB, edits are human-paced).
struct PatternUndo : history::ModuleAction {
    std::shared_ptr<PatternBank> before, after;
    void apply(const PatternBank& bank)
    {
        if (auto* m = dynamic_cast<Tetrad*>(APP->engine->getModule(moduleId)))
            m->replacePatterns(bank);
    }
    void undo() override { apply(*before); }
    void redo() override { apply(*after); }
};

// One undo entry per gesture (a click, a whole drag, a key command).
struct Gesture {
    std::shared_ptr<PatternBank> before;

    void begin(Tetrad* m)
    {
        before = std::make_shared<PatternBank>();
        m->copyPatterns(*before);
    }

    // Returns the undo action, or null if the patterns did not change.
    PatternUndo* take(Tetrad* m, const std::string& name)
    {
        if (!before)
            return nullptr;
        auto after = std::make_shared<PatternBank>();
        m->copyPatterns(*after);
        PatternUndo* a = nullptr;
        if (std::memcmp(before.get(), after.get(), sizeof(PatternBank)) != 0) {
            a = new PatternUndo;
            a->name = name;
            a->moduleId = m->id;
            a->before = before;
            a->after = after;
        }
        before.reset();
        return a;
    }

    void end(Tetrad* m, const std::string& name)
    {
        if (PatternUndo* a = take(m, name))
            APP->history->push(a);
    }
};

// Parameters already changed and an optional pattern edit, as one undo entry (none if nothing
// moved). `olds` are the values before the change.
inline void pushUndo(Module* m, const std::string& name, const std::vector<std::pair<int, float>>& olds,
                     history::Action* patterns = nullptr)
{
    auto* complex = new history::ComplexAction;
    complex->name = name;
    if (patterns)
        complex->push(patterns);
    for (const auto& [id, old] : olds) {
        const float now = m->params[id].getValue();
        if (now == old)
            continue;
        auto* a = new history::ParamChange;
        a->moduleId = m->id;
        a->paramId = id;
        a->oldValue = old;
        a->newValue = now;
        complex->push(a);
    }
    if (complex->isEmpty())
        delete complex;
    else
        APP->history->push(complex);
}

inline void setParamWithUndo(Module* m, int id, float value)
{
    if (!m)
        return;
    ParamQuantity* q = m->getParamQuantity(id);
    const float old = q->getValue();
    q->setValue(value);
    pushUndo(m, "change " + q->getLabel(), {{id, old}});
}

// A pattern edit as one undo entry; returns true if it changed the patterns.
template <typename F>
bool editWithUndo(Tetrad* m, const std::string& name, F&& fn)
{
    if (!m)
        return false;
    Gesture g;
    g.begin(m);
    const bool changed = m->editPatterns(fn);
    g.end(m, name);
    return changed;
}

// Editing actions shared by the inspector buttons, the keyboard and the menus (Actions.cpp).
void newSeed(Tetrad* m, int track);
void euclidLoop(Tetrad* m, const UiState& u);
void clearLoopOrSelection(Tetrad* m, const UiState& u);
void rotateLoop(Tetrad* m, int track, int amount);
void duplicateLoop(Tetrad* m, int track);
void appendLoopTools(ui::Menu* menu, Tetrad* m, UiState* u);

} // namespace ui_tetrad

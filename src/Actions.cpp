// Editing actions on the loop window (Start / Length) and the selection. Each one is a single
// undo entry.

#include "ui.hpp"

#include <random>

namespace ui_tetrad {

namespace {

int scaledDivision(int division, double factor) { return divisionForBeats(divisionBeats(division) * factor); }

// A pattern edit plus new parameter values, as one undo entry.
template <typename F>
void editWithParams(Tetrad* m, const std::string& name, F&& fn, std::initializer_list<std::pair<int, float>> values)
{
    Gesture g;
    g.begin(m);
    m->editPatterns(fn);
    std::vector<std::pair<int, float>> olds;
    for (const auto& [id, v] : values) {
        olds.push_back({id, m->params[id].getValue()});
        m->params[id].setValue(v);
    }
    pushUndo(m, name, olds, g.take(m, name));
}

} // namespace

void newSeed(Tetrad* m, int track)
{
    static std::minstd_rand rng(std::random_device{}());
    setParamWithUndo(m, trackParamIndex(track, kTrackSeed), float(rng() % 10000));
}

// On the bottom visible row of the NOTE lane.
void euclidLoop(Tetrad* m, const UiState& u)
{
    const int t = u.view.track, start = loopStart(m, t), len = loopLength(m, t);
    const int pulses = u.euclidPulses[t], rotation = u.euclidRotation[t], degree = u.view.lowestDegree;
    editWithUndo(m, "euclid", [&](PatternBank& b) {
        euclid(b, t, start, len, pulses, rotation, degree);
        return true;
    });
}

void clearLoopOrSelection(Tetrad* m, const UiState& u)
{
    const int t = u.view.track;
    const StepMask mask = u.selection.any() ? u.selection : windowMask(loopStart(m, t), loopLength(m, t));
    editWithUndo(m, "clear steps", [&](PatternBank& b) {
        clearSteps(b, t, mask);
        return true;
    });
}

void rotateLoop(Tetrad* m, int track, int amount)
{
    const StepMask w = windowMask(loopStart(m, track), loopLength(m, track));
    editWithUndo(m, "rotate loop", [&](PatternBank& b) {
        shiftSteps(b, track, w, amount);
        return true;
    });
}

// Copies the loop after itself and doubles its length.
void duplicateLoop(Tetrad* m, int track)
{
    const int start = loopStart(m, track), len = loopLength(m, track);
    if (!m || len * 2 > kMaxSteps)
        return;
    editWithParams(m, "duplicate loop", [&](PatternBank& b) {
        Clipboard clip;
        copySteps(b, track, windowMask(start, len), clip);
        pasteSteps(b, track, start + len, clip);
        return true;
    }, {{trackParamIndex(track, kTrackLength), float(len * 2)}});
}

void appendLoopTools(ui::Menu* menu, Tetrad* m, UiState* u)
{
    if (!m)
        return;
    const int t = u->view.track;
    const int lenId = trackParamIndex(t, kTrackLength), divId = trackParamIndex(t, kTrackDivision);
    const int start = loopStart(m, t), len = loopLength(m, t);
    const int div = trackValue(m, t, kTrackDivision);

    menu->addChild(createSubmenuItem(string::f("Loop tools (track %d, steps %d-%d)", t + 1, start + 1, start + len), "",
                                     [=](ui::Menu* sub) {
        const int half = scaledDivision(div, 0.5), twice = scaledDivision(div, 2.0);
        sub->addChild(createMenuItem("Expand (x2 length, half step)", "", [=] {
            editWithParams(m, "expand", [&](PatternBank& b) { return expandSteps(b, t, start, len); },
                           {{lenId, float(len * 2)}, {divId, float(half)}});
        }, len * 2 > kMaxSteps || half < 0));
        sub->addChild(createMenuItem("Condense (half length, x2 step)", "", [=] {
            editWithParams(m, "condense", [&](PatternBank& b) {
                condenseSteps(b, t, start, len);
                return true;
            }, {{lenId, float((len + 1) / 2)}, {divId, float(twice)}});
        }, len < 2 || twice < 0));
        sub->addChild(createMenuItem("Flip (reverse)", "", [=] {
            editWithUndo(m, "flip", [&](PatternBank& b) {
                reverseSteps(b, t, start, len);
                return true;
            });
        }));
        sub->addChild(createMenuItem("Rotate left", "Shift+Left", [=] { rotateLoop(m, t, -1); }));
        sub->addChild(createMenuItem("Rotate right", "Shift+Right", [=] { rotateLoop(m, t, 1); }));
        sub->addChild(createMenuItem("Duplicate (x2 length)", RACK_MOD_CTRL_NAME "+D", [=] { duplicateLoop(m, t); },
                                     len * 2 > kMaxSteps));
        sub->addChild(createMenuItem(string::f("Euclid %d hits / rotation %d", u->euclidPulses[t], u->euclidRotation[t]),
                                     "E", [=] { euclidLoop(m, *u); }));
        sub->addChild(new MenuSeparator);
        sub->addChild(createMenuItem("Clear the loop", "", [=] {
            editWithUndo(m, "clear loop", [&](PatternBank& b) {
                clearSteps(b, t, windowMask(start, len));
                return true;
            });
        }));
    }));
}

} // namespace ui_tetrad

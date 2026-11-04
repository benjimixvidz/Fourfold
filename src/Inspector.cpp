#include "Inspector.hpp"

namespace ui_tetrad {

namespace {

constexpr float kW = 228, kH = 286;
constexpr float kColX[3] = {4, 79, 154};
constexpr float kCellW = 72, kRowH = 15, kCellH = 14;
constexpr float kTrackY = 52, kScaleY = 129, kGenY = 160, kSelY = 224;
constexpr float kDefaultTempo = 120.f;

enum class Ref { Track, Global, Step };

struct FieldDef {
    Ref ref;
    int id;   // TrackParam, GlobalParam or StepField
    const char* label;
    float y;
    int col;
    int span;
    const char* hint;
};

const FieldDef kFields[] = {
    {Ref::Track, kTrackMode, "MODE", kTrackY, 0, 1, "Mono: one note per step · Poly: up to 7 (also drum rows on the Pipe)"},
    {Ref::Track, kTrackFollow, "TRSP IN", kTrackY, 1, 1, "The TRANSP jack transposes this track"},
    {Ref::Track, kTrackEnabled, "ON", kTrackY, 2, 1, "Mute: the track keeps running silently (key M)"},
    {Ref::Track, kTrackDivision, "DIV", kTrackY + kRowH, 0, 1, "Step length"},
    {Ref::Track, kTrackDirection, "DIR", kTrackY + kRowH, 1, 1, "Forward, backward, ping-pong, random, drunk"},
    {Ref::Track, kTrackSwing, "SWNG", kTrackY + kRowH, 2, 1, "50 = straight · above: odd steps later · below: earlier"},
    {Ref::Track, kTrackLength, "LEN", kTrackY + 2 * kRowH, 0, 1, "Loop length (also: drag the bars of the overview)"},
    {Ref::Track, kTrackStart, "START", kTrackY + 2 * kRowH, 1, 1, "First step of the loop"},
    {Ref::Track, kTrackOctave, "OCT", kTrackY + 2 * kRowH, 2, 1, "Base octave (4 = C4)"},
    {Ref::Track, kTrackTranspose, "TRSP", kTrackY + 3 * kRowH, 0, 1, "Transpose in semitones"},
    {Ref::Track, kTrackVelocity, "VEL", kTrackY + 3 * kRowH, 1, 1, "Velocity scale of the whole track"},
    {Ref::Track, kTrackGate, "GATE", kTrackY + 3 * kRowH, 2, 1, "Gate length of the whole track (> 100 % = legato)"},
    {Ref::Track, kTrackChance, "PROB", kTrackY + 4 * kRowH, 0, 1, "Probability scale of the whole track"},
    {Ref::Global, kGlobalKey, "ROOT", kScaleY, 0, 1, "Root of the scale (all tracks)"},
    {Ref::Global, kGlobalScale, "SCALE", kScaleY, 1, 2, "Scale (all tracks): steps are scale degrees"},
    {Ref::Track, kTrackSeed, "SEED", kGenY, 0, 1, "Generator seed: changing it rewrites the track (undo: Ctrl+Z)"},
    {Ref::Track, kTrackDensity, "DENS", kGenY, 1, 1, "Share of active steps"},
    {Ref::Track, kTrackRange, "RNG", kGenY, 2, 1, "Range in scale degrees"},
    {Ref::Track, kTrackVoices, "VOIC", kGenY + kRowH, 0, 1, "Notes per step (Poly)"},
    {Ref::Track, kTrackRatchet, "RTCH", kGenY + kRowH, 1, 1, "Share of steps with ratchets"},
    {Ref::Track, kTrackVariation, "VARI", kGenY + kRowH, 2, 1, "Share of steps with trig conditions"},
    {Ref::Step, kStepVelocity, "VEL", kSelY, 0, 1, "Velocity of the selected steps"},
    {Ref::Step, kStepGate, "GATE", kSelY, 1, 1, "Gate of the selected steps (past 100: tie)"},
    {Ref::Step, kStepProbability, "PROB", kSelY, 2, 1, "Probability of the selected steps"},
    {Ref::Step, kStepRatchet, "RTCH", kSelY + kRowH, 0, 1, "Ratchets of the selected steps"},
    {Ref::Step, kStepCondition, "COND", kSelY + kRowH, 1, 2, "Trig condition of the selected steps"},
    {Ref::Step, kStepCondA, "A", kSelY + 2 * kRowH, 0, 1, "A of A:B"},
    {Ref::Step, kStepCondB, "B", kSelY + 2 * kRowH, 1, 1, "B of A:B"},
    {Ref::Step, kStepNotes, "NOTES", kSelY + 2 * kRowH, 2, 1, "Notes per step (Poly)"},
};
constexpr int kNumFields = int(sizeof(kFields) / sizeof(kFields[0]));

math::Rect fieldRect(const FieldDef& f)
{
    const float w = kCellW * f.span + (f.span - 1) * (kColX[1] - kColX[0] - kCellW);
    return {{kColX[f.col], f.y}, {w, kCellH}};
}

int fieldAt(Vec p)
{
    for (int i = 0; i < kNumFields; ++i)
        if (fieldRect(kFields[i]).contains(p))
            return i;
    return -1;
}

const math::Rect kTempoRect{{94, 2}, {78, 30}};
const math::Rect kClockRect{{174, 2}, {52, 14}};
const math::Rect kPpqnRect{{174, 18}, {52, 14}};

enum Button { kNewSeed, kEuclid, kPulses, kRotation, kClear, kNumButtons };
const math::Rect kButtonRects[kNumButtons] = {
    {{4, 192}, {54, 16}}, {{60, 192}, {52, 16}}, {{114, 192}, {34, 16}}, {{150, 192}, {34, 16}}, {{186, 192}, {40, 16}},
};
const char* const kButtonHints[kNumButtons] = {
    "New random seed: regenerates the track (undo restores it)",
    "Euclidean rhythm over the loop, on the bottom row (key E)",
    "Euclid hits: drag",
    "Euclid rotation: drag",
    "Clear the selection, or the whole loop (key Delete with a selection)",
};

int buttonAt(Vec p)
{
    for (int i = 0; i < kNumButtons; ++i)
        if (kButtonRects[i].contains(p))
            return i;
    return -1;
}

// hoverField values for the transport areas.
constexpr int kHoverTempo = -2, kHoverPpqn = -3;

} // namespace

Inspector::Inspector() { box.size = Vec(kW, kH); }

int Inspector::paramId(int field) const
{
    const FieldDef& f = kFields[field];
    return f.ref == Ref::Track ? trackParamIndex(ui->view.track, f.id) : f.ref == Ref::Global ? globalParamIndex(f.id) : -1;
}

// "75", or "64-100" when the selected steps differ.
std::string Inspector::stepText(int field) const
{
    const auto& steps = ui->bank.tracks[ui->view.track].steps;
    const int first = firstStep(ui->selection);
    if (first < 0)
        return "-";
    int lo = stepField(steps[first], field), hi = lo;
    for (int i = first + 1; i < kMaxSteps; ++i)
        if (ui->selection[size_t(i)]) {
            const int v = stepField(steps[i], field);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    auto one = [&](int v) -> std::string {
        switch (field) {
        case kStepGate: return v > 100 ? "TIE" : string::f("%d%%", v);
        case kStepProbability: return string::f("%d%%", v);
        case kStepRatchet: return string::f("x%d", v);
        case kStepCondition:
            if (v == kCondAB || v == kCondNotAB)
                return string::f("%s%d:%d", v == kCondNotAB ? "NOT " : "", steps[first].condA, steps[first].condB);
            return conditionName(v);
        default: return string::f("%d", v);
        }
    };
    return lo == hi ? one(lo) : one(lo) + "-" + one(hi);
}

void Inspector::drawLayer(const DrawArgs& args, int layer)
{
    if (layer != 1)
        return OpaqueWidget::drawLayer(args, layer);
    NVGcontext* vg = args.vg;
    setFont(vg);
    const int t = ui->view.track;
    const NVGcolor tc = kTrackColors[t];
    fillRect(vg, {{0, 0}, box.size}, kScreen, 3.f);

    // Transport: PLAY and RESET are Rack widgets placed over the left corner (kPlayRect, kResetRect).
    const bool internal = !module || !module->externalClock.load(std::memory_order_relaxed);
    const double bpm = !module ? kDefaultTempo
                       : internal ? double(module->params[Tetrad::TEMPO_PARAM].getValue())
                                  : module->tempoNow.load(std::memory_order_relaxed);
    if (hoverField == kHoverTempo || dragKind == Drag::Tempo)
        fillRect(vg, kTempoRect, kButton, 2.f);
    text(vg, kTempoRect.pos.x + 2, kTempoRect.pos.y + 16, bpm > 0 ? string::f("%.1f", bpm) : "--.-", kInk, 22.f);
    text(vg, kTempoRect.pos.x + kTempoRect.size.x - 2, kTempoRect.pos.y + 25, "BPM", kTextDim, 8.f,
         NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    fillRect(vg, kClockRect, kField, 2.f);
    text(vg, kClockRect.pos.x + kClockRect.size.x - 3, kClockRect.pos.y + kClockRect.size.y / 2 + 0.5f,
         internal ? "INT CLK" : "EXT CLK", internal ? kInk : tc, 9.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    fillRect(vg, kPpqnRect, hoverField == kHoverPpqn ? kButtonHover : kButton, 2.f);
    centered(vg, kPpqnRect, string::f("%d PPQN", kPpqnChoices[module ? module->ppqnIndex.load() : 0]), kText, 9.f);

    const bool on = trackValue(module, t, kTrackEnabled) != 0;
    fillRect(vg, {{2, 36}, {kW - 4, 14}}, nvgTransRGBAf(tc, 0.25f), 2.f);
    fillRect(vg, {{2, 36}, {4, 14}}, tc, 1.f);
    text(vg, 10, 43, string::f("TRACK %d%s", t + 1, on ? "" : "  · MUTED"), on ? kInk : kTextDim, 10.f);

    text(vg, 6, 152, "GENERATOR", kTextDim, 8.f);
    const int selected = int(ui->selection.count());
    text(vg, 6, 216, selected ? string::f("SELECTION · %d STEP%s", selected, selected > 1 ? "S" : "")
                              : "SELECTION · Shift+drag the grid",
         selected ? kInk : kTextDim, 8.f);

    char buf[32];
    for (int i = 0; i < kNumFields; ++i) {
        const FieldDef& f = kFields[i];
        const math::Rect r = fieldRect(f);
        const bool isStep = f.ref == Ref::Step;
        const bool enabled = !isStep || selected;
        const bool active = i == hoverField || i == dragField;
        fillRect(vg, r, active && enabled ? kButtonHover : kField, 2.f);
        text(vg, r.pos.x + 3, r.pos.y + r.size.y / 2, f.label, enabled ? kTextDim : kTextOff, 8.f);
        std::string v;
        if (isStep) {
            if (selected)
                v = stepText(f.id);
        } else {
            const int id = paramId(i);
            formatParam(id, paramValue(module, id), buf, sizeof(buf));
            v = buf;
        }
        text(vg, r.pos.x + r.size.x - 3, r.pos.y + r.size.y / 2, v, enabled ? kInk : kTextDim, 9.5f,
             NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        if (i == dragField)
            fillRect(vg, {{r.pos.x, r.pos.y + r.size.y - 2}, {r.size.x, 2}}, tc, 1.f);
    }

    const std::string labels[kNumButtons] = {"NEW SEED", "EUCLID", string::f("%d HITS", ui->euclidPulses[t]),
                                             string::f("ROT %d", ui->euclidRotation[t]), selected ? "CLR SEL" : "CLEAR"};
    for (int i = 0; i < kNumButtons; ++i) {
        fillRect(vg, kButtonRects[i], hoverButton == i ? kButtonHover : kButton, 2.f);
        centered(vg, kButtonRects[i], labels[i], kText, 8.5f);
    }

    // Help line: what the pointer can do here.
    const math::Rect hl{{2, kH - 15}, {kW - 4, 13}};
    fillRect(vg, hl, kField, 2.f);
    nvgSave(vg);
    nvgScissor(vg, hl.pos.x, hl.pos.y, hl.size.x, hl.size.y);
    text(vg, hl.pos.x + 3, hl.pos.y + hl.size.y / 2, ui->hint, kTextDim, 7.5f);
    nvgRestore(vg);
    OpaqueWidget::drawLayer(args, layer);
}

// Relative change during a drag; the undo entry is made at the end of the drag.
void Inspector::nudgeField(int field, int delta)
{
    if (!module)
        return;
    const FieldDef& f = kFields[field];
    if (f.ref == Ref::Step) {
        const int t = ui->view.track;
        const StepMask sel = ui->selection;
        module->editPatterns([&](PatternBank& b) { return adjustSteps(b, t, sel, f.id, delta); });
        return;
    }
    const int id = paramId(field);
    const ParamDesc& d = paramTable()[id];
    Param& p = module->params[id];
    const float v = d.type == ParamType::Bool ? float(delta > 0) : std::round(p.getValue()) + delta;
    p.setValue(clamp(v, float(d.min), float(d.max)));
}

void Inspector::resetField(int field)
{
    const FieldDef& f = kFields[field];
    if (f.ref != Ref::Step) {
        const int id = paramId(field);
        setParamWithUndo(module, id, float(paramTable()[id].def));
        return;
    }
    const int t = ui->view.track;
    const StepMask sel = ui->selection;
    editWithUndo(module, "reset steps", [&](PatternBank& b) { return resetSteps(b, t, sel, f.id); });
}

// Every value of a list, or a slider and a typed entry for numbers.
void Inspector::fieldMenu(int field)
{
    if (!module)
        return;
    const FieldDef& f = kFields[field];
    Tetrad* m = module;
    if (f.ref == Ref::Step) {
        if (ui->selection.none())
            return;
        std::vector<std::pair<std::string, int>> choices;
        switch (f.id) {
        case kStepVelocity:
            for (int v : {32, 48, 64, 75, 90, 100, 110, 127})
                choices.push_back({string::f("%d", v), v});
            break;
        case kStepGate:
            for (int v : {10, 25, 50, 75, 90, 100})
                choices.push_back({string::f("%d%%", v), v});
            choices.push_back({"Tie", kGateTie});
            break;
        case kStepProbability:
            for (int v : {10, 25, 33, 50, 66, 75, 90, 100})
                choices.push_back({string::f("%d%%", v), v});
            break;
        case kStepRatchet:
            for (int v = 1; v <= kMaxRatchet; ++v)
                choices.push_back({string::f("x%d", v), v});
            break;
        case kStepCondition:
            for (int v = 0; v < kCondCount; ++v)
                choices.push_back({conditionName(v), v});
            break;
        case kStepCondA:
        case kStepCondB:
            for (int v = 1; v <= 8; ++v)
                choices.push_back({string::f("%d", v), v});
            break;
        case kStepNotes:
            for (int v = 1; v <= kMaxVoices; ++v)
                choices.push_back({string::f("%d", v), v});
            break;
        default: break;
        }
        ui::Menu* menu = createMenu();
        menu->addChild(createMenuLabel(f.hint));
        const StepMask sel = ui->selection;
        const int t = ui->view.track, which = f.id;
        for (const auto& [label, v] : choices)
            menu->addChild(createMenuItem(label, "", [=] {
                editWithUndo(m, "edit steps", [&](PatternBank& b) { return setSteps(b, t, sel, which, v); });
            }));
        return;
    }
    ui::Menu* menu = createMenu();
    menu->addChild(createMenuLabel(f.hint));
    const int id = paramId(field);
    ParamQuantity* q = m->getParamQuantity(id);
    if (auto* sq = dynamic_cast<SwitchQuantity*>(q)) {
        for (size_t i = 0; i < sq->labels.size(); ++i) {
            const float v = sq->getMinValue() + float(i);
            menu->addChild(createCheckMenuItem(sq->labels[i], "", [=] { return m->params[id].getValue() == v; },
                                               [=] { setParamWithUndo(m, id, v); }));
        }
        return;
    }
    struct ValueEntry : ui::TextField {
        Module* module;
        int paramId;
        void onSelectKey(const SelectKeyEvent& e) override
        {
            if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
                ParamQuantity* q = module->getParamQuantity(paramId);
                const float old = q->getValue();
                q->setDisplayValueString(text);
                pushUndo(module, "set " + q->getLabel(), {{paramId, old}});
                if (auto* overlay = getAncestorOfType<ui::MenuOverlay>())
                    overlay->requestDelete();
                e.consume(this);
                return;
            }
            ui::TextField::onSelectKey(e);
        }
    };
    auto* slider = new ui::Slider;
    slider->quantity = q;
    slider->box.size.x = 200.f;
    menu->addChild(slider);
    auto* entry = new ValueEntry;
    entry->module = m;
    entry->paramId = id;
    entry->box.size.x = 200.f;
    entry->text = q->getDisplayValueString();
    menu->addChild(createMenuLabel("Type a value, then Enter:"));
    menu->addChild(entry);
    APP->event->setSelectedWidget(entry);
    entry->selectAll();
}

void Inspector::ppqnMenu()
{
    if (!module)
        return;
    Tetrad* m = module;
    ui::Menu* menu = createMenu();
    menu->addChild(createMenuLabel("Clock input resolution (pulses per quarter note)"));
    for (int i = 0; i < kNumPpqnChoices; ++i)
        menu->addChild(createCheckMenuItem(string::f("%d PPQN%s", kPpqnChoices[i], i == 0 ? "  (most clock modules)" : ""),
                                           "", [=] { return m->ppqnIndex.load() == i; }, [=] { m->ppqnIndex.store(i); }));
}

void Inspector::pressButton(int button)
{
    switch (button) {
    case kNewSeed: newSeed(module, ui->view.track); break;
    case kEuclid: euclidLoop(module, *ui); break;
    case kClear: clearLoopOrSelection(module, *ui); break;
    default: break;
    }
}

void Inspector::onButton(const ButtonEvent& e)
{
    if (e.action != GLFW_PRESS)
        return OpaqueWidget::onButton(e);
    const Vec p = e.pos;
    const int field = fieldAt(p);
    if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (field >= 0)
            fieldMenu(field);
        else if (kPpqnRect.contains(p))
            ppqnMenu();
        else
            return OpaqueWidget::onButton(e);
        e.consume(this);
        return;
    }
    if (e.button != GLFW_MOUSE_BUTTON_LEFT)
        return OpaqueWidget::onButton(e);
    e.consume(this);
    dragAccum = 0.f;
    dragMoved = false;
    dragField = -1;
    dragKind = Drag::None;
    dragFine = (e.mods & RACK_MOD_MASK) & RACK_MOD_CTRL;
    if (!module)
        return;

    if (field >= 0) {
        const bool isStep = kFields[field].ref == Ref::Step;
        if (isStep && ui->selection.none())
            return;
        dragField = field;
        dragKind = Drag::Field;
        if (isStep)
            gesture.begin(module);
        else
            dragOld = module->params[paramId(field)].getValue();
    } else if (kTempoRect.contains(p)) {
        if (!module->externalClock.load(std::memory_order_relaxed)) {
            dragKind = Drag::Tempo;
            dragOld = module->params[Tetrad::TEMPO_PARAM].getValue();
        }
    } else if (kPpqnRect.contains(p)) {
        ppqnMenu();
    } else {
        const int b = buttonAt(p);
        if (b == kPulses || b == kRotation)
            dragKind = b == kPulses ? Drag::Pulses : Drag::Rotation;
        else
            pressButton(b);
    }
}

void Inspector::onDragMove(const DragMoveEvent& e)
{
    if (dragKind == Drag::None)
        return;
    const float detent = dragFine ? 18.f : 6.f;
    dragAccum -= e.mouseDelta.y / getAbsoluteZoom();
    const int delta = int(dragAccum / detent);
    if (!delta)
        return;
    dragAccum -= delta * detent;
    dragMoved = true;
    const int t = ui->view.track;
    const int len = loopLength(module, t);
    switch (dragKind) {
    case Drag::Field: nudgeField(dragField, delta); break;
    case Drag::Tempo: {
        Param& p = module->params[Tetrad::TEMPO_PARAM];
        p.setValue(clamp(p.getValue() + delta * (dragFine ? 0.1f : 1.f), 20.f, 300.f));
        break;
    }
    case Drag::Pulses: ui->euclidPulses[t] = clamp(ui->euclidPulses[t] + delta, 0, len); break;
    case Drag::Rotation: ui->euclidRotation[t] = ((ui->euclidRotation[t] + delta) % len + len) % len; break;
    default: break;
    }
}

void Inspector::onDragEnd(const DragEndEvent& e)
{
    if (dragKind == Drag::Field) {
        if (kFields[dragField].ref == Ref::Step) {
            gesture.end(module, "edit steps");
        } else if (dragMoved) {
            const int id = paramId(dragField);
            pushUndo(module, "change " + module->getParamQuantity(id)->getLabel(), {{id, dragOld}});
        } else {
            // A click without motion: toggle a switch, list the values of an enum.
            const int id = paramId(dragField);
            const ParamType type = paramTable()[id].type;
            if (type == ParamType::Bool)
                setParamWithUndo(module, id, module->params[id].getValue() > 0.5f ? 0.f : 1.f);
            else if (type == ParamType::Enum)
                fieldMenu(dragField);
        }
    } else if (dragKind == Drag::Tempo && dragMoved) {
        pushUndo(module, "change tempo", {{Tetrad::TEMPO_PARAM, dragOld}});
    }
    dragKind = Drag::None;
    dragField = -1;
}

// Rack's double-click event has no position: the last hover position stands in.
void Inspector::onDoubleClick(const DoubleClickEvent& e)
{
    e.consume(this);
    if (!module)
        return;
    // Lists and switches act on the first click already; their default is in their list.
    const int field = fieldAt(lastHover);
    const bool isStep = field >= 0 && kFields[field].ref == Ref::Step;
    if (isStep ? ui->selection.any() : field >= 0 && paramTable()[paramId(field)].type == ParamType::Int)
        resetField(field);
    else if (kTempoRect.contains(lastHover) && !module->externalClock.load(std::memory_order_relaxed))
        setParamWithUndo(module, Tetrad::TEMPO_PARAM, kDefaultTempo);
}

void Inspector::onHover(const HoverEvent& e)
{
    lastHover = e.pos;
    hoverField = fieldAt(e.pos);
    hoverButton = buttonAt(e.pos);
    if (hoverField >= 0) {
        const FieldDef& f = kFields[hoverField];
        const ParamType type = f.ref == Ref::Step ? ParamType::Int : paramTable()[paramId(hoverField)].type;
        ui->hint = std::string(f.hint) + (type == ParamType::Bool   ? " · click: toggle"
                                          : type == ParamType::Enum ? " · click: list"
                                                                    : " · drag · dbl: default · right: values");
    } else if (kTempoRect.contains(e.pos)) {
        hoverField = kHoverTempo;
        ui->hint = "Tempo: drag (Ctrl: 0.1) · dbl: 120 · used when CLOCK is not patched";
    } else if (kPpqnRect.contains(e.pos)) {
        hoverField = kHoverPpqn;
        ui->hint = "Resolution of the CLOCK input: click to choose";
    } else if (hoverButton >= 0) {
        ui->hint = kButtonHints[hoverButton];
    }
    OpaqueWidget::onHover(e);
}

void Inspector::onLeave(const LeaveEvent& e)
{
    hoverField = -1;
    hoverButton = -1;
    OpaqueWidget::onLeave(e);
}

} // namespace ui_tetrad

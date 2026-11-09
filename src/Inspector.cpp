#include "Inspector.hpp"

namespace ui_tetrad {

namespace {

constexpr float kW = 228, kH = 286;
constexpr float kColX[3] = {4, 79, 154};
constexpr float kCellW = 72, kRowH = 15, kCellH = 14;
constexpr float kDefaultTempo = 120.f;

// Vertical layout of the sections, for the main view (EXPERT off) and every setting (on).
struct Layout {
    float track, scale, genLabel, gen, buttons, selLabel, sel;
};
constexpr Layout kMain{52, 90, 113, 121, 140, 164, 172};
constexpr Layout kExpert{52, 129, 152, 160, 192, 216, 224};

// Row: the drum row settings of a Drum track (they take the place of the root and scale).
enum class Ref { Track, Global, Step, Row };
enum RowField { kRowFocus, kRowLevel, kRowProbability };
enum Section { kSecTrack, kSecScale, kSecGen, kSecSel };

struct Place {
    int row, col, span;   // row -1: not shown in this view
};

struct FieldDef {
    Ref ref;
    int id;   // TrackParam, GlobalParam or StepField
    const char* label;
    Section section;
    Place expert, main;
    const char* hint;
};

constexpr Place kHidden{-1, 0, 1};

// The main view keeps what shapes a line at once: mode, division, length, octave, gate, the
// scale, the generator's seed, density and range, and the selected steps' velocity, gate and
// probability. EXPERT shows the rest.
const FieldDef kFields[] = {
    {Ref::Track, kTrackMode, "MODE", kSecTrack, {0, 0, 1}, {0, 0, 1}, "Mono: one note per step · Poly: up to 7 (also drum rows on the Pipe)"},
    {Ref::Track, kTrackFollow, "TRSP IN", kSecTrack, {0, 1, 1}, kHidden, "The TRANSP jack transposes this track"},
    {Ref::Track, kTrackEnabled, "ON", kSecTrack, {0, 2, 1}, {0, 2, 1}, "Mute: the track keeps running silently (key M)"},
    {Ref::Track, kTrackDivision, "DIV", kSecTrack, {1, 0, 1}, {0, 1, 1}, "Step length"},
    {Ref::Track, kTrackDirection, "DIR", kSecTrack, {1, 1, 1}, kHidden, "Forward, backward, ping-pong, random, drunk"},
    {Ref::Track, kTrackSwing, "SWNG", kSecTrack, {1, 2, 1}, kHidden, "50 = straight · above: odd steps later · below: earlier"},
    {Ref::Track, kTrackLength, "LEN", kSecTrack, {2, 0, 1}, {1, 0, 1}, "Loop length (also: drag the bars of the overview)"},
    {Ref::Track, kTrackStart, "START", kSecTrack, {2, 1, 1}, kHidden, "First step of the loop"},
    {Ref::Track, kTrackOctave, "OCT", kSecTrack, {2, 2, 1}, {1, 1, 1}, "Base octave (4 = C4)"},
    {Ref::Track, kTrackTranspose, "TRSP", kSecTrack, {3, 0, 1}, kHidden, "Transpose in semitones"},
    {Ref::Track, kTrackVelocity, "VEL", kSecTrack, {3, 1, 1}, kHidden, "Velocity scale of the whole track"},
    {Ref::Track, kTrackGate, "GATE", kSecTrack, {3, 2, 1}, {1, 2, 1}, "Gate length of the whole track (> 100 % = legato)"},
    {Ref::Track, kTrackChance, "PROB", kSecTrack, {4, 0, 1}, kHidden, "Probability scale of the whole track"},
    {Ref::Global, kGlobalKey, "ROOT", kSecScale, {0, 0, 1}, {0, 0, 1}, "Root of the scale (all tracks)"},
    {Ref::Global, kGlobalScale, "SCALE", kSecScale, {0, 1, 2}, {0, 1, 2}, "Scale (all tracks): steps are scale degrees"},
    {Ref::Track, kTrackSeed, "SEED", kSecGen, {0, 0, 1}, {0, 0, 1}, "Generator seed: changing it rewrites the track (undo: Ctrl+Z)"},
    {Ref::Track, kTrackDensity, "DENS", kSecGen, {0, 1, 1}, {0, 1, 1}, "Share of active steps"},
    {Ref::Track, kTrackRange, "RNG", kSecGen, {0, 2, 1}, {0, 2, 1}, "Range in scale degrees"},
    {Ref::Track, kTrackVoices, "VOIC", kSecGen, {1, 0, 1}, kHidden, "Notes per step (Poly)"},
    {Ref::Track, kTrackRatchet, "RTCH", kSecGen, {1, 1, 1}, kHidden, "Share of steps with ratchets"},
    {Ref::Track, kTrackVariation, "VARI", kSecGen, {1, 2, 1}, kHidden, "Share of steps with trig conditions"},
    {Ref::Step, kStepVelocity, "VEL", kSecSel, {0, 0, 1}, {0, 0, 1}, "Velocity of the selected steps"},
    {Ref::Step, kStepGate, "GATE", kSecSel, {0, 1, 1}, {0, 1, 1}, "Gate of the selected steps (past 100: tie)"},
    {Ref::Step, kStepProbability, "PROB", kSecSel, {0, 2, 1}, {0, 2, 1}, "Probability of the selected steps"},
    {Ref::Step, kStepRatchet, "RTCH", kSecSel, {1, 0, 1}, kHidden, "Ratchets of the selected steps"},
    {Ref::Step, kStepCondition, "COND", kSecSel, {1, 1, 2}, kHidden, "Trig condition of the selected steps"},
    {Ref::Step, kStepCondA, "A", kSecSel, {2, 0, 1}, kHidden, "A of A:B"},
    {Ref::Step, kStepCondB, "B", kSecSel, {2, 1, 1}, kHidden, "B of A:B"},
    {Ref::Step, kStepNotes, "NOTES", kSecSel, {2, 2, 1}, kHidden, "Notes per step (Poly)"},
    {Ref::Row, kRowFocus, "ROW", kSecScale, {0, 0, 1}, {0, 0, 1}, "Drum row edited by the lanes and the selection (Up / Down)"},
    {Ref::Row, kRowLevel, "LVL", kSecScale, {0, 1, 1}, {0, 1, 1}, "Velocity scale of every hit of this row"},
    {Ref::Row, kRowProbability, "PROB", kSecScale, {0, 2, 1}, {0, 2, 1}, "Probability scale of every hit of this row"},
};

// A Drum track shows its row settings instead of the root and scale (which its rows ignore).
bool shownFor(const FieldDef& f, bool drum)
{
    return f.ref == Ref::Global ? !drum : f.ref == Ref::Row ? drum : true;
}

// Settings a Drum track ignores: drawn dimmed.
bool appliesTo(const FieldDef& f, bool drum)
{
    if (!drum)
        return true;
    if (f.ref == Ref::Track)
        return f.id != kTrackOctave && f.id != kTrackTranspose && f.id != kTrackFollow && f.id != kTrackRange &&
               f.id != kTrackVoices;
    return !(f.ref == Ref::Step && f.id == kStepNotes);
}
constexpr int kNumFields = int(sizeof(kFields) / sizeof(kFields[0]));

const Layout& layoutFor(bool expert) { return expert ? kExpert : kMain; }

float sectionY(const Layout& l, Section s)
{
    switch (s) {
    case kSecTrack: return l.track;
    case kSecScale: return l.scale;
    case kSecGen: return l.gen;
    default: return l.sel;
    }
}

// Where the field sits in this view; false if the view leaves it out.
bool fieldRect(const FieldDef& f, bool expert, bool drum, math::Rect& r)
{
    const Place& p = expert ? f.expert : f.main;
    if (p.row < 0 || !shownFor(f, drum))
        return false;
    const float w = kCellW * p.span + (p.span - 1) * (kColX[1] - kColX[0] - kCellW);
    r = {{kColX[p.col], sectionY(layoutFor(expert), f.section) + p.row * kRowH}, {w, kCellH}};
    return true;
}

int fieldAt(Vec p, bool expert, bool drum)
{
    math::Rect r;
    for (int i = 0; i < kNumFields; ++i)
        if (fieldRect(kFields[i], expert, drum, r) && r.contains(p))
            return i;
    return -1;
}

const math::Rect kTempoRect{{94, 2}, {78, 30}};
const math::Rect kClockRect{{174, 2}, {52, 14}};
const math::Rect kPpqnRect{{174, 18}, {52, 14}};

enum Button { kNewSeed, kEuclid, kPulses, kRotation, kClear, kNumButtons };
math::Rect buttonRect(int i, bool expert)
{
    static const float x[kNumButtons] = {4, 60, 114, 150, 186}, w[kNumButtons] = {54, 52, 34, 34, 40};
    return {{x[i], layoutFor(expert).buttons}, {w[i], 16}};
}
const char* const kButtonHints[kNumButtons] = {
    "New random seed: regenerates the track (undo restores it)",
    "Euclidean rhythm over the loop, on the bottom row; Drum: on the focused row (key E)",
    "Euclid hits: drag",
    "Euclid rotation: drag",
    "Clear the selection, or the whole loop (key Delete with a selection)",
};

int buttonAt(Vec p, bool expert)
{
    for (int i = 0; i < kNumButtons; ++i)
        if (buttonRect(i, expert).contains(p))
            return i;
    return -1;
}

// hoverField values for the transport areas.
constexpr int kHoverTempo = -2, kHoverPpqn = -3;

} // namespace

Inspector::Inspector() { box.size = Vec(kW, kH); }

bool Inspector::expert() const { return module && module->params[Tetrad::EXPERT_PARAM].getValue() > 0.5f; }

bool Inspector::drum() const { return trackValue(module, ui->view.track, kTrackMode) == kModeDrum; }

int Inspector::hitRow() const { return drum() ? clamp(ui->view.focusRow, 0, kDrumRows - 1) : -1; }

std::string Inspector::rowText(int field) const
{
    const RowSettings& rs = ui->bank.tracks[ui->view.track].rows[clamp(ui->view.focusRow, 0, kDrumRows - 1)];
    switch (field) {
    case kRowFocus: return string::f("%d", ui->view.focusRow + 1);
    case kRowLevel: return string::f("%d%%", rs.level);
    default: return string::f("%d%%", rs.probability);
    }
}

// The focused row (UI state) or one of its settings (in the patterns, so undo covers them).
void Inspector::nudgeRow(int field, int delta)
{
    if (field == kRowFocus) {
        ui->view.focusRow = clamp(ui->view.focusRow + delta, 0, kDrumRows - 1);
        return;
    }
    const int t = ui->view.track, r = clamp(ui->view.focusRow, 0, kDrumRows - 1);
    module->editPatterns([&](PatternBank& b) {
        RowSettings& rs = b.tracks[t].rows[r];
        uint8_t& v = field == kRowLevel ? rs.level : rs.probability;
        const int next = clamp(int(v) + delta, 0, field == kRowLevel ? 200 : 100);
        if (next == v)
            return false;
        v = uint8_t(next);
        return true;
    });
}

void Inspector::setRow(int field, int value)
{
    if (field == kRowFocus) {
        ui->view.focusRow = clamp(value, 0, kDrumRows - 1);
        return;
    }
    const int t = ui->view.track, r = clamp(ui->view.focusRow, 0, kDrumRows - 1);
    editWithUndo(module, field == kRowLevel ? "row level" : "row probability", [&](PatternBank& b) {
        RowSettings& rs = b.tracks[t].rows[r];
        (field == kRowLevel ? rs.level : rs.probability) = uint8_t(value);
        return true;
    });
}

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
    const int row = hitRow();
    if (row >= 0 && field == kStepNotes)
        return "-";
    int lo = stepField(steps[first], field, row), hi = lo;
    for (int i = first + 1; i < kMaxSteps; ++i)
        if (ui->selection[size_t(i)]) {
            const int v = stepField(steps[i], field, row);
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
                return string::f("%s%d:%d", v == kCondNotAB ? "NOT " : "", stepField(steps[first], kStepCondA, row),
                                 stepField(steps[first], kStepCondB, row));
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

    const bool ex = expert();
    const Layout& lay = layoutFor(ex);
    text(vg, 6, lay.genLabel, "GENERATOR", kTextDim, 8.f);
    const int selected = int(ui->selection.count());
    text(vg, 6, lay.selLabel, selected ? string::f("SELECTION · %d STEP%s", selected, selected > 1 ? "S" : "")
                              : "SELECTION · Shift+drag the grid",
         selected ? kInk : kTextDim, 8.f);

    char buf[32];
    const bool isDrum = drum();
    for (int i = 0; i < kNumFields; ++i) {
        const FieldDef& f = kFields[i];
        math::Rect r;
        if (!fieldRect(f, ex, isDrum, r))
            continue;
        const bool isStep = f.ref == Ref::Step;
        const bool enabled = (!isStep || selected) && appliesTo(f, isDrum);
        const bool active = i == hoverField || i == dragField;
        fillRect(vg, r, active && enabled ? kButtonHover : kField, 2.f);
        text(vg, r.pos.x + 3, r.pos.y + r.size.y / 2, f.label, enabled ? kTextDim : kTextOff, 8.f);
        std::string v;
        if (isStep) {
            if (selected)
                v = stepText(f.id);
        } else if (f.ref == Ref::Row) {
            v = rowText(f.id);
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
        const math::Rect r = buttonRect(i, ex);
        fillRect(vg, r, hoverButton == i ? kButtonHover : kButton, 2.f);
        centered(vg, r, labels[i], kText, 8.5f);
    }

    // Help line: what the pointer can do here (nothing drawn before the first hover: an empty
    // bar looked like a text field).
    const math::Rect hl{{2, kH - 15}, {kW - 4, 13}};
    if (!ui->hint.empty())
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
    if (f.ref == Ref::Row) {
        nudgeRow(f.id, delta);
        return;
    }
    if (f.ref == Ref::Step) {
        const int t = ui->view.track, row = hitRow();
        const StepMask sel = ui->selection;
        module->editPatterns([&](PatternBank& b) { return adjustSteps(b, t, sel, f.id, delta, row); });
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
    if (f.ref == Ref::Row) {
        setRow(f.id, f.id == kRowFocus ? 0 : 100);
        return;
    }
    if (f.ref != Ref::Step) {
        const int id = paramId(field);
        setParamWithUndo(module, id, float(paramTable()[id].def));
        return;
    }
    const int t = ui->view.track, row = hitRow();
    const StepMask sel = ui->selection;
    editWithUndo(module, "reset steps", [&](PatternBank& b) { return resetSteps(b, t, sel, f.id, row); });
}

// Every value of a list, or a slider and a typed entry for numbers.
void Inspector::fieldMenu(int field)
{
    if (!module)
        return;
    const FieldDef& f = kFields[field];
    Tetrad* m = module;
    if (f.ref == Ref::Row) {
        ui::Menu* menu = createMenu();
        menu->addChild(createMenuLabel(f.hint));
        const int which = f.id;
        std::vector<int> values;
        if (which == kRowFocus)
            values = {0, 1, 2, 3, 4, 5, 6, 7};
        else if (which == kRowLevel)
            values = {25, 50, 75, 100, 125, 150};
        else
            values = {10, 25, 50, 75, 90, 100};
        for (int v : values)
            menu->addChild(createMenuItem(which == kRowFocus ? string::f("Row %d", v + 1) : string::f("%d%%", v), "",
                                          [=, this] { setRow(which, v); }));
        return;
    }
    if (f.ref == Ref::Step) {
        if (ui->selection.none() || !appliesTo(f, drum()))
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
        const int t = ui->view.track, which = f.id, row = hitRow();
        for (const auto& [label, v] : choices)
            menu->addChild(createMenuItem(label, "", [=] {
                editWithUndo(m, "edit steps", [&](PatternBank& b) { return setSteps(b, t, sel, which, v, row); });
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
    menu->addChild(new ParamSlider(m, id, 200.f));
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
    const int field = fieldAt(p, expert(), drum());
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
        const FieldDef& f = kFields[field];
        const bool isStep = f.ref == Ref::Step;
        if ((isStep && ui->selection.none()) || !appliesTo(f, drum()))
            return;
        dragField = field;
        dragKind = Drag::Field;
        if (isStep || f.ref == Ref::Row)
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
        const int b = buttonAt(p, expert());
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
        const FieldDef& f = kFields[dragField];
        if (f.ref == Ref::Row) {
            gesture.end(module, f.id == kRowLevel ? "row level" : "row probability");
            if (!dragMoved && f.id == kRowFocus)
                fieldMenu(dragField);   // a click lists the rows
        } else if (f.ref == Ref::Step) {
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
    const int field = fieldAt(lastHover, expert(), drum());
    const bool isStep = field >= 0 && kFields[field].ref == Ref::Step;
    const bool isRow = field >= 0 && kFields[field].ref == Ref::Row;
    if (isRow ? kFields[field].id != kRowFocus
              : isStep ? ui->selection.any() : field >= 0 && paramTable()[paramId(field)].type == ParamType::Int)
        resetField(field);
    else if (kTempoRect.contains(lastHover) && !module->externalClock.load(std::memory_order_relaxed))
        setParamWithUndo(module, Tetrad::TEMPO_PARAM, kDefaultTempo);
}

void Inspector::onHover(const HoverEvent& e)
{
    lastHover = e.pos;
    hoverField = fieldAt(e.pos, expert(), drum());
    hoverButton = buttonAt(e.pos, expert());
    if (hoverField >= 0) {
        const FieldDef& f = kFields[hoverField];
        const ParamType type = f.ref == Ref::Row ? (f.id == kRowFocus ? ParamType::Enum : ParamType::Int)
                               : f.ref == Ref::Step ? ParamType::Int
                                                    : paramTable()[paramId(hoverField)].type;
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

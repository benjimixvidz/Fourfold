#include "GridPanel.hpp"

namespace ui_tetrad {

namespace {

constexpr float kW = 588;
constexpr float kTabH = 20;
constexpr float kOvY = 24, kOvH = 20, kOvW = 560;
constexpr float kGridY = 48, kColW = 35, kRowH = 30, kPadW = 32, kPadH = 27;
constexpr float kLabelX = 562;
constexpr float kStepW = kOvW / kMaxSteps;

math::Rect trackTab(int i) { return {{i * 58.f, 0}, {54, kTabH}}; }
math::Rect laneTab(int i) { return {{240 + i * 58.f, 0}, {54, kTabH}}; }
math::Rect overviewRect() { return {{0, kOvY}, {kOvW, kOvH}}; }
math::Rect followRect() { return {{kLabelX, kOvY}, {kW - kLabelX, kOvH}}; }
math::Rect padRect(int col, int row) { return {{col * kColW, kGridY + row * kRowH}, {kPadW, kPadH}}; }
math::Rect gridRect() { return {{0, kGridY}, {kGridCols * kColW, kGridRows * kRowH}}; }
math::Rect labelRect() { return {{kLabelX, kGridY}, {kW - kLabelX, kGridRows * kRowH}}; }

int wrapStep(int s) { return ((s % kMaxSteps) + kMaxSteps) % kMaxSteps; }

} // namespace

GridPanel::GridPanel() { box.size = Vec(kW, kGridY + kGridRows * kRowH); }

bool GridPanel::poly() const { return trackValue(module, ui->view.track, kTrackMode) == kModePoly; }

int GridPanel::playhead() const
{
    return module ? module->playheadSteps[ui->view.track].load(std::memory_order_relaxed) : 0;
}

bool GridPanel::playing() const { return module && module->running.load(std::memory_order_relaxed); }

void GridPanel::step()
{
    if (ui->follow && playing() && mode == Mode::None)
        ui->view.page = playhead() / kGridCols;
    OpaqueWidget::step();
}

void GridPanel::drawLayer(const DrawArgs& args, int layer)
{
    if (layer != 1)
        return OpaqueWidget::drawLayer(args, layer);
    NVGcontext* vg = args.vg;
    setFont(vg);
    const int t = ui->view.track;
    const NVGcolor tc = kTrackColors[t];
    const PatternBank& bank = ui->bank;
    double values[kNumParams];
    for (int i = 0; i < kNumParams; ++i)
        values[i] = paramValue(module, i);
    const Settings settings = settingsFromValues(values);

    // Track tabs: colour + fill when selected; "MUTE" in red when muted.
    for (int i = 0; i < kNumTracks; ++i) {
        const math::Rect r = trackTab(i);
        const bool on = settings.tracks[i].enabled;
        const bool sel = i == t;
        fillRect(vg, r, sel ? kTrackColors[i] : kButton);
        if (!sel)
            fillRect(vg, {r.pos, {3, r.size.y}}, kTrackColors[i], 1.f);
        const bool trackPoly = settings.tracks[i].mode == kModePoly;
        centered(vg, r, string::f("T%d %s", i + 1, !on ? "MUTE" : trackPoly ? "POLY" : "MONO"),
                 sel ? kTextDark : !on ? kMuted : kText, 10.f);
    }
    for (int i = 0; i < kLaneCount; ++i) {
        const math::Rect r = laneTab(i);
        const bool sel = i == ui->view.lane;
        fillRect(vg, r, sel ? kButtonOn : (hoverLane == i ? kButtonHover : kButton));
        centered(vg, r, kLaneNames[i], kText, 10.f);
        if (sel)
            fillRect(vg, {{r.pos.x + 4, r.pos.y + r.size.y - 3}, {r.size.x - 8, 2}}, kText, 1.f);
    }

    // Overview: loop window, active steps, pages, viewed page, playhead.
    const math::Rect ov = overviewRect();
    fillRect(vg, ov, kScreen, 2.f);
    // Shapes of one colour go into one path: one fill per colour instead of one per step.
    const int start = loopStart(module, t), len = loopLength(module, t);
    nvgBeginPath(vg);
    const int firstPart = std::min(len, kMaxSteps - start);   // the window may wrap past step 128
    nvgRect(vg, ov.pos.x + start * kStepW, ov.pos.y + 2, firstPart * kStepW, ov.size.y - 4);
    if (len > firstPart)
        nvgRect(vg, ov.pos.x, ov.pos.y + 2, (len - firstPart) * kStepW, ov.size.y - 4);
    nvgFillColor(vg, nvgTransRGBAf(tc, 0.22f));
    nvgFill(vg);
    nvgBeginPath(vg);
    for (int s = 0; s < kMaxSteps; ++s) {
        const Step& st = bank.tracks[t].steps[s];
        if (!st.active)
            continue;
        const float h = 4.f + 10.f * st.velocity / 127.f;
        nvgRect(vg, ov.pos.x + s * kStepW + 0.5f, ov.pos.y + ov.size.y - 3 - h, kStepW - 1.f, h);
    }
    nvgFillColor(vg, tc);
    nvgFill(vg);
    nvgBeginPath(vg);
    for (int p = 1; p < kGridPages; ++p) {
        nvgMoveTo(vg, ov.pos.x + p * kGridCols * kStepW, ov.pos.y + 2);
        nvgLineTo(vg, ov.pos.x + p * kGridCols * kStepW, ov.pos.y + ov.size.y - 2);
    }
    nvgStrokeColor(vg, kButtonOn);
    nvgStrokeWidth(vg, 1.f);
    nvgStroke(vg);
    strokeRect(vg, {{ov.pos.x + ui->view.page * kGridCols * kStepW, ov.pos.y}, {kGridCols * kStepW, ov.size.y}}, kText,
               1.5f, 2.f);
    // Loop handles: bars at the first and after the last step of the window.
    for (int edge = 0; edge < 2; ++edge) {
        const int s = edge == 0 ? start : wrapStep(start + len);
        const float x = ov.pos.x + (edge == 1 && s == 0 ? kMaxSteps : s) * kStepW;
        fillRect(vg, {{x - 1.5f, ov.pos.y}, {3, ov.size.y}}, tc, 1.f);
    }
    if (playing()) {
        const float x = ov.pos.x + (playhead() + 0.5f) * kStepW;
        nvgBeginPath(vg);
        nvgMoveTo(vg, x - 3, ov.pos.y);
        nvgLineTo(vg, x + 3, ov.pos.y);
        nvgLineTo(vg, x, ov.pos.y + 5);
        nvgClosePath(vg);
        nvgFillColor(vg, kSelect);
        nvgFill(vg);
    }
    const math::Rect fr = followRect();
    fillRect(vg, fr, ui->follow ? tc : kButton);
    centered(vg, fr, "F", ui->follow ? kTextDark : kText, 11.f);

    // Grid.
    const int size = scaleSize(settings.global.scale);
    const bool isPoly = poly();
    const int head = playhead();
    const bool headOnPage = playing() && head / kGridCols == ui->view.page;
    // Pads are sorted by look, then each look is filled in one go.
    enum Look { kLookOff, kLookOutside, kLookRoot, kLookHead, kLookLit, kLookLitDim, kLookCount };
    const NVGcolor looks[kLookCount] = {kPadOff, kPadOutside, kPadRoot, kPlayhead, tc, nvgLerpRGBA(kPadOff, tc, 0.45f)};
    uint8_t look[kGridRows][kGridCols];
    bool litAtHead[kGridRows] = {};
    for (int row = 0; row < kGridRows; ++row) {
        for (int col = 0; col < kGridCols; ++col) {
            const PadInfo info = padInfo(bank, ui->view, isPoly, size, start, len, col, row);
            const bool atHead = headOnPage && head % kGridCols == col;
            if (info.lit)
                look[row][col] = (info.active || ui->view.lane == kLaneNote) ? kLookLit : kLookLitDim;
            else if (atHead)
                look[row][col] = kLookHead;
            else if (!info.inLoop)
                look[row][col] = kLookOutside;
            else if (ui->view.lane == kLaneNote && info.rootRow)
                look[row][col] = kLookRoot;
            else
                look[row][col] = kLookOff;
            litAtHead[row] |= atHead && info.lit;
        }
    }
    for (int l = 0; l < kLookCount; ++l) {
        nvgBeginPath(vg);
        bool any = false;
        for (int row = 0; row < kGridRows; ++row)
            for (int col = 0; col < kGridCols; ++col)
                if (look[row][col] == l) {
                    const math::Rect r = padRect(col, row);
                    nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, 3.f);
                    any = true;
                }
        if (any) {
            nvgFillColor(vg, looks[l]);
            nvgFill(vg);
        }
    }
    if (headOnPage)
        for (int row = 0; row < kGridRows; ++row)
            if (litAtHead[row])
                strokeRect(vg, padRect(head % kGridCols, row), kSelect, 1.f, 3.f);
    if (hoverCol >= 0 && hoverRow >= 0 && mode == Mode::None)
        strokeRect(vg, padRect(hoverCol, hoverRow), nvgTransRGBAf(kText, 0.6f), 1.f, 3.f);
    // Selection: a white frame around each selected column, with a corner mark (not colour only).
    for (int col = 0; col < kGridCols; ++col) {
        if (!ui->selection[size_t(stepForColumn(ui->view, col))])
            continue;
        const math::Rect r{{col * kColW - 1.5f, kGridY - 2}, {kPadW + 3, kGridRows * kRowH + 1}};
        strokeRect(vg, r, kSelect, 2.f, 4.f);
        fillRect(vg, {{r.pos.x + r.size.x - 7, r.pos.y + 1}, {6, 6}}, kSelect, 1.f);
    }

    // Row labels: note names in the NOTE lane, values elsewhere; markers for notes out of view.
    char buf[16];
    for (int row = 0; row < kGridRows; ++row) {
        const math::Rect r = padRect(0, row);
        std::string s;
        if (ui->view.lane == kLaneNote)
            s = noteName(trackKey(settings, t, ui->view.lowestDegree + kGridRows - 1 - row), buf, sizeof(buf));
        else
            s = rowLabel(ui->view, row, buf, sizeof(buf));
        text(vg, kLabelX + 3, r.pos.y + kPadH / 2, s, kTextDim, 9.f);
    }
    if (ui->view.lane == kLaneNote) {
        int above = 0, below = 0;
        for (int i = 0; i < len; ++i) {
            const Step& st = bank.tracks[t].steps[wrapStep(start + i)];
            if (!st.active)
                continue;
            const int n = isPoly ? st.numNotes : 1;
            for (int v = 0; v < n; ++v) {
                above += st.degree[v] > ui->view.lowestDegree + kGridRows - 1;
                below += st.degree[v] < ui->view.lowestDegree;
            }
        }
        auto tri = [&](float y, bool up) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, kW - 8, y + (up ? 0 : 6));
            nvgLineTo(vg, kW - 2, y + (up ? 0 : 6));
            nvgLineTo(vg, kW - 5, y + (up ? 6 : 0));
            nvgClosePath(vg);
            nvgFillColor(vg, tc);
            nvgFill(vg);
        };
        if (above)
            tri(kGridY + 2, true);
        if (below)
            tri(kGridY + kGridRows * kRowH - 10, false);
    }
    OpaqueWidget::drawLayer(args, layer);
}

bool GridPanel::cellAt(Vec p, int& col, int& row) const
{
    if (!gridRect().contains(p))
        return false;
    col = clamp(int(p.x / kColW), 0, kGridCols - 1);
    row = clamp(int((p.y - kGridY) / kRowH), 0, kGridRows - 1);
    return true;
}

int GridPanel::overviewStep(float x) const { return clamp(int(x / kStepW), 0, kMaxSteps - 1); }

void GridPanel::paintCell(int col, int row, bool first)
{
    if (!module)
        return;
    const int stepIndex = stepForColumn(ui->view, col);
    const bool isPoly = poly();
    const GridView view = ui->view;
    const int degree = view.lowestDegree + kGridRows - 1 - row;
    module->editPatterns([&](PatternBank& b) {
        Step& s = b.tracks[view.track].steps[stepIndex];
        if (view.lane != kLaneNote)
            return pressPad(b, view, isPoly, col, row);
        const int n = isPoly ? clamp(int(s.numNotes), 1, kMaxVoices) : 1;
        const bool lit = s.active && std::find(s.degree, s.degree + n, int8_t(degree)) != s.degree + n;
        if (first)
            paintOn = !lit;
        if (paintOn == lit && (isPoly || !paintOn))
            return false;   // already in the painted state
        if (!isPoly && !paintOn) {
            s.active = false;   // erasing a mono line: any note of the column goes
            return true;
        }
        if (!isPoly) {
            s.active = true;
            s.degree[0] = int8_t(degree);
            return true;
        }
        return pressPad(b, view, true, col, row);
    });
    lastCol = col;
    lastRow = row;
}

void GridPanel::selectTo(int col)
{
    const int step = stepForColumn(ui->view, col);
    if (ui->anchor < 0)
        ui->anchor = step;
    ui->selection = rangeMask(ui->anchor, step);
}

void GridPanel::stepMenu(int col)
{
    if (!module)
        return;
    const int stepIndex = stepForColumn(ui->view, col);
    Tetrad* m = module;
    UiState* u = ui;
    const int t = u->view.track;
    ui::Menu* menu = createMenu();
    menu->addChild(createMenuLabel(string::f("Track %d · step %d", t + 1, stepIndex + 1)));
    menu->addChild(createMenuItem("Select this step", "", [=] {
        u->selection.reset();
        u->selection.set(size_t(stepIndex));
        u->anchor = stepIndex;
    }));
    menu->addChild(createMenuItem("Toggle on/off", "", [=] {
        editWithUndo(m, "toggle step", [&](PatternBank& b) {
            b.tracks[t].steps[stepIndex].active = !b.tracks[t].steps[stepIndex].active;
            return true;
        });
    }));
    const Step& step = u->bank.tracks[t].steps[stepIndex];
    menu->addChild(createSubmenuItem("Ratchet", string::f("x%d", step.ratchet), [=](ui::Menu* sub) {
        for (int r = 1; r <= kMaxRatchet; ++r)
            sub->addChild(createMenuItem(string::f("x%d", r), "", [=] {
                editWithUndo(m, "ratchet", [&](PatternBank& b) {
                    b.tracks[t].steps[stepIndex].ratchet = uint8_t(r);
                    return true;
                });
            }));
    }));
    menu->addChild(createSubmenuItem("Condition", conditionName(step.condition), [=](ui::Menu* sub) {
        struct C {
            const char* name;
            uint8_t cond, a, b;
        };
        const C list[] = {{"Always", kCondAlways, 1, 1}, {"1:2", kCondAB, 1, 2},  {"2:2", kCondAB, 2, 2},
                          {"1:4", kCondAB, 1, 4},        {"2:4", kCondAB, 2, 4},  {"3:4", kCondAB, 3, 4},
                          {"4:4", kCondAB, 4, 4},        {"Not 1:2", kCondNotAB, 1, 2}, {"1st", kCondFirst, 1, 1},
                          {"Not 1st", kCondNotFirst, 1, 1}, {"PRE", kCondPre, 1, 1}, {"Not PRE", kCondNotPre, 1, 1}};
        for (const C& c : list)
            sub->addChild(createMenuItem(c.name, "", [=] {
                editWithUndo(m, "condition", [&](PatternBank& b) {
                    Step& s = b.tracks[t].steps[stepIndex];
                    s.condition = c.cond;
                    s.condA = c.a;
                    s.condB = c.b;
                    return true;
                });
            }));
    }));
    menu->addChild(new MenuSeparator);
    menu->addChild(createMenuItem("Clear step", "", [=] {
        StepMask one;
        one.set(size_t(stepIndex));
        editWithUndo(m, "clear step", [&](PatternBank& b) {
            clearSteps(b, t, one);
            return true;
        });
    }));
    menu->addChild(new MenuSeparator);
    appendLoopTools(menu, m, u);
    menu->addChild(createMenuItem("Paste here", RACK_MOD_CTRL_NAME "+V", [=] {
        editWithUndo(m, "paste steps", [&](PatternBank& b) { return pasteSteps(b, t, stepIndex, u->clipboard).any(); });
    }, u->clipboard.count == 0));
}

void GridPanel::onButton(const ButtonEvent& e)
{
    const Vec p = e.pos;
    if (e.action != GLFW_PRESS)
        return OpaqueWidget::onButton(e);
    int col, row;
    const bool inGrid = cellAt(p, col, row);

    if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (inGrid) {
            stepMenu(col);
            e.consume(this);
            return;
        }
        return OpaqueWidget::onButton(e);
    }
    if (e.button != GLFW_MOUSE_BUTTON_LEFT)
        return OpaqueWidget::onButton(e);
    e.consume(this);
    pressPos = dragPos = p;

    for (int i = 0; i < kNumTracks; ++i)
        if (trackTab(i).contains(p)) {
            ui->selectTrack(i);
            return;
        }
    for (int i = 0; i < kLaneCount; ++i)
        if (laneTab(i).contains(p)) {
            ui->view.lane = i;
            return;
        }
    if (followRect().contains(p)) {
        ui->follow = !ui->follow;
        return;
    }
    if (overviewRect().contains(p)) {
        const int s = overviewStep(p.x);
        const int start = loopStart(module, ui->view.track), len = loopLength(module, ui->view.track);
        const int end = wrapStep(start + len);   // first step after the window
        const float sx = start * kStepW, ex = (end == 0 ? kMaxSteps : end) * kStepW;
        dragStart = start;
        dragLength = len;
        if (std::fabs(p.x - sx) <= 4) {
            mode = Mode::LoopStart;
        } else if (std::fabs(p.x - ex) <= 4) {
            mode = Mode::LoopEnd;
        } else if ((s - start + kMaxSteps) % kMaxSteps < len) {
            mode = Mode::LoopMove;
            grabOffset = (s - start + kMaxSteps) % kMaxSteps;
        } else {
            mode = Mode::None;
        }
        // A click (not a handle) also shows that page; following stops.
        if (mode != Mode::LoopStart && mode != Mode::LoopEnd) {
            ui->view.page = s / kGridCols;
            ui->follow = false;
        }
        return;
    }
    if (labelRect().contains(p)) {
        mode = Mode::Scroll;
        scrollAccum = 0.f;
        return;
    }
    if (!inGrid)
        return;

    const int mods = e.mods & RACK_MOD_MASK;
    if (mods & GLFW_MOD_SHIFT) {
        // Shift extends the current selection from its anchor, or starts a new one here.
        if (ui->selection.none())
            ui->anchor = -1;
        mode = Mode::Select;
        selectTo(col);
        return;
    }
    if (mods & RACK_MOD_CTRL) {
        const int s = stepForColumn(ui->view, col);
        ui->selection.flip(size_t(s));
        ui->anchor = s;
        return;
    }
    if (!module)
        return;
    mode = Mode::Paint;
    gesture.begin(module);
    paintCell(col, row, true);
    ui->anchor = stepForColumn(ui->view, col);
}

void GridPanel::onDragMove(const DragMoveEvent& e)
{
    dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
    switch (mode) {
    case Mode::Paint: {
        int col, row;
        if (cellAt(dragPos, col, row) && (col != lastCol || row != lastRow))
            paintCell(col, row, false);
        break;
    }
    case Mode::Select: {
        int col, row;
        if (cellAt(Vec(clamp(dragPos.x, 0.f, kGridCols * kColW - 1), kGridY + 1), col, row))
            selectTo(col);
        break;
    }
    case Mode::LoopStart:
    case Mode::LoopEnd:
    case Mode::LoopMove: {
        if (!module)
            break;
        const int s = overviewStep(clamp(dragPos.x, 0.f, kOvW - 0.01f));
        int start = dragStart, len = dragLength;
        if (mode == Mode::LoopStart) {
            const int lastStep = wrapStep(dragStart + dragLength - 1);
            start = s;
            len = (lastStep - start + kMaxSteps) % kMaxSteps + 1;
        } else if (mode == Mode::LoopEnd) {
            len = clamp((s - dragStart + kMaxSteps) % kMaxSteps + 1, 1, kMaxSteps);
        } else {
            start = wrapStep(s - grabOffset);
        }
        module->params[trackParamIndex(ui->view.track, kTrackStart)].setValue(float(start + 1));
        module->params[trackParamIndex(ui->view.track, kTrackLength)].setValue(float(len));
        break;
    }
    case Mode::Scroll: {
        scrollAccum += e.mouseDelta.y / getAbsoluteZoom();
        while (std::fabs(scrollAccum) >= kRowH / 2) {
            scroll(scrollAccum > 0 ? 1 : -1);   // drag down = see higher notes, like grabbing the grid
            scrollAccum -= scrollAccum > 0 ? kRowH / 2 : -kRowH / 2;
        }
        break;
    }
    default:
        break;
    }
}

void GridPanel::onDragEnd(const DragEndEvent& e)
{
    if (mode == Mode::Paint && module)
        gesture.end(module, "edit steps");
    if ((mode == Mode::LoopStart || mode == Mode::LoopEnd || mode == Mode::LoopMove) && module) {
        const int t = ui->view.track;
        pushUndo(module, "move loop",
                 {{trackParamIndex(t, kTrackStart), float(dragStart + 1)}, {trackParamIndex(t, kTrackLength), float(dragLength)}});
    }
    mode = Mode::None;
}

// Loop start (0-based) and length as one undo entry.
void GridPanel::setLoop(int start, int length, const std::string& name)
{
    if (!module)
        return;
    const int startId = trackParamIndex(ui->view.track, kTrackStart);
    const int lengthId = trackParamIndex(ui->view.track, kTrackLength);
    const float oldStart = module->params[startId].getValue(), oldLength = module->params[lengthId].getValue();
    module->params[startId].setValue(float(start + 1));
    module->params[lengthId].setValue(float(length));
    pushUndo(module, name, {{startId, oldStart}, {lengthId, oldLength}});
}

void GridPanel::onDoubleClick(const DoubleClickEvent& e)
{
    // Rack's double-click event has no position: the press position stands in.
    if (overviewRect().contains(pressPos)) {
        const int page = overviewStep(pressPos.x) / kGridCols;
        setLoop(page * kGridCols, kGridCols, "loop page");
        mode = Mode::None;   // the second press started a loop drag: it must not move the new loop
        e.consume(this);
        return;
    }
    OpaqueWidget::onDoubleClick(e);
}

void GridPanel::scroll(int delta)
{
    int& low = ui->lowest[ui->view.track];
    low = clamp(low + delta, kGridMinDegree, kGridMaxDegree);
    ui->view.lowestDegree = low;
}

void GridPanel::onHoverScroll(const HoverScrollEvent& e)
{
    // Only the row-label column scrolls notes: the rest of the module leaves the wheel to Rack.
    if (labelRect().contains(e.pos) && ui->view.lane == kLaneNote && e.scrollDelta.y != 0) {
        scroll(e.scrollDelta.y > 0 ? 1 : -1);
        e.consume(this);
        return;
    }
    OpaqueWidget::onHoverScroll(e);
}

void GridPanel::onHover(const HoverEvent& e)
{
    hoverCol = hoverRow = hoverLane = -1;
    int col, row;
    if (cellAt(e.pos, col, row)) {
        hoverCol = col;
        hoverRow = row;
        ui->hint = "Click: on/off · Drag: paint · Shift+drag: select · Ctrl+click: add to selection · Right-click: step menu";
    } else if (overviewRect().contains(e.pos)) {
        ui->hint = "Click: show page · Double-click: loop this page · Drag the bars: loop start / end · Drag inside: move the loop";
    } else if (labelRect().contains(e.pos)) {
        ui->hint = "Drag or wheel: scroll notes (also with the arrow buttons of the keyboard)";
    } else {
        for (int i = 0; i < kLaneCount; ++i)
            if (laneTab(i).contains(e.pos))
                hoverLane = i;
        ui->hint = hoverLane >= 0 ? "Lane shortcuts: N V G P R C" : "Tracks: keys 1-4 · M: mute · F: follow";
    }
    OpaqueWidget::onHover(e);
}

void GridPanel::onLeave(const LeaveEvent& e)
{
    hoverCol = hoverRow = hoverLane = -1;
    OpaqueWidget::onLeave(e);
}

} // namespace ui_tetrad

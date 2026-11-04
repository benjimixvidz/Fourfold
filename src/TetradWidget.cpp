// Tetrad panel (56 HP): inspector on the left, grid on the right, every jack on one row at the
// bottom. Keyboard shortcuts work while the pointer is over the module; keys Rack uses (Delete,
// Ctrl+C...) are only taken when there is a selection to act on.

#include "GridPanel.hpp"
#include "Inspector.hpp"

namespace ui_tetrad {

namespace {

// Step field edited by Up / Down in each lane (NOTE transposes instead).
const StepField kLaneField[kLaneCount] = {kStepVelocity, kStepVelocity, kStepGate,
                                          kStepProbability, kStepRatchet, kStepCondition};

struct Labels : TransparentWidget {
    struct Item {
        Vec pos;
        std::string text;
        float size;
        NVGcolor color;
    };
    std::vector<Item> items;
    void add(Vec pos, const std::string& t, float size, NVGcolor color = kText) { items.push_back({pos, t, size, color}); }
    void draw(const DrawArgs& args) override
    {
        if (!setFont(args.vg))
            return;
        for (const Item& it : items)
            text(args.vg, it.pos.x, it.pos.y, it.text, it.color, it.size, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
};

// Every gesture and shortcut, over the whole panel (key H or the HELP button; click to close).
struct HelpOverlay : OpaqueWidget {
    UiState* ui = nullptr;

    void step() override
    {
        visible = ui && ui->help;
        OpaqueWidget::step();
    }

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if (layer != 1)
            return;
        NVGcontext* vg = args.vg;
        fillRect(vg, {{0, 0}, box.size}, nvgRGBA(6, 8, 10, 236), 4.f);
        setFont(vg);
        // Lines starting with '#' are headings.
        const char* left[] = {
            "#GRID",
            "Click            on / off (NOTE) · set the value (lanes)",
            "Drag             paint notes, draw bars",
            "Shift+drag       select steps · Ctrl+click: add/remove",
            "Right-click      step menu: ratchet, condition, clear, paste",
            "Row labels       drag or wheel: scroll notes",
            "",
            "#OVERVIEW (128 steps)",
            "Click            show that page (follow off)",
            "Drag the bars    loop start / end · drag inside: move",
            "Double-click     loop on that page",
            "#INSPECTOR (left)",
            "Drag a value     change it (Ctrl: fine)",
            "Double-click     default value (numbers)",
            "Right-click      every value · numbers: type one",
            "Selection        fields edit every selected step",
        };
        const char* right[] = {
            "#KEYBOARD (pointer over the module)",
            "Space · T        play / stop · tap tempo",
            "1 2 3 4          track",
            "N V G P R C      lane note, vel, gate, prob, ratchet, cond",
            "M · F · H        mute · follow · this help",
            "E · S            Euclid on the loop · new seed",
            "Left / Right     page · with selection: move the steps",
            "Shift+Left/Right rotate the whole loop",
            "Up / Down        selection: transpose / value · else scroll",
            "Shift+Up/Down    octave / x10",
            "Ctrl+A · Esc     select loop · deselect",
            "Ctrl+C X V       copy · cut · paste at the selection",
            "Ctrl+D           duplicate the loop (length x2)",
            "Delete           clear the selection",
            "Ctrl+Z           undo (Rack history) · Ctrl+Shift+Z redo",
        };
        auto column = [&](float x, const char* const* lines, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                const bool heading = lines[i][0] == '#';
                text(vg, x, 22 + 16 * i, lines[i] + heading, heading ? kInk : kText, 10.f);
            }
        };
        column(16, left, sizeof(left) / sizeof(left[0]));
        column(430, right, sizeof(right) / sizeof(right[0]));
        text(vg, box.size.x / 2, box.size.y - 14, "click anywhere to close", kTextDim, 9.f,
             NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }

    void onButton(const ButtonEvent& e) override
    {
        if (e.action == GLFW_PRESS)
            ui->help = false;
        e.consume(this);
    }
};

// PLAY (latch) and RESET (momentary) drawn as large buttons. They stay Rack parameters, so
// tooltips, MIDI mapping and undo work as for any switch.
struct TransportButton : app::Switch {
    UiState* ui = nullptr;
    bool isPlay = false;
    bool hovered = false;

    TransportButton() { box.size = kPlayRect.size; }

    void onEnter(const EnterEvent& e) override
    {
        hovered = true;
        ui->hint = isPlay ? "Play / stop (key Space); stop goes back to step 1" : "Back to step 1";
        Switch::onEnter(e);
    }

    void onLeave(const LeaveEvent& e) override
    {
        hovered = false;
        Switch::onLeave(e);
    }

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if (layer != 1)
            return Switch::drawLayer(args, layer);
        NVGcontext* vg = args.vg;
        ParamQuantity* q = getParamQuantity();
        const bool on = q ? q->getValue() > 0.5f : isPlay;
        const NVGcolor fill = on ? (isPlay ? kPlayOn : kButtonOn) : hovered ? kButtonHover : kButton;
        const NVGcolor ink = on ? kTextDark : kText;
        fillRect(vg, {{0, 0}, box.size}, fill, 3.f);
        // Icon: a triangle for PLAY, a bar and a triangle pointing left for RESET.
        const float cx = box.size.x / 2, cy = 11;
        nvgBeginPath(vg);
        if (isPlay) {
            nvgMoveTo(vg, cx - 5, cy - 6);
            nvgLineTo(vg, cx + 6, cy);
            nvgLineTo(vg, cx - 5, cy + 6);
        } else {
            nvgRect(vg, cx - 7, cy - 6, 2, 12);
            nvgMoveTo(vg, cx + 6, cy - 6);
            nvgLineTo(vg, cx - 4, cy);
            nvgLineTo(vg, cx + 6, cy + 6);
        }
        nvgClosePath(vg);
        nvgFillColor(vg, on || !isPlay ? ink : kPlayOn);
        nvgFill(vg);
        setFont(vg);
        text(vg, cx, 24, isPlay ? "PLAY" : "RESET", ink, 8.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
};

// HELP, at the top right of the panel: every gesture and shortcut (also key H).
struct HelpButton : OpaqueWidget {
    UiState* ui = nullptr;
    bool hovered = false;

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if (layer != 1)
            return;
        NVGcontext* vg = args.vg;
        fillRect(vg, {{0, 0}, box.size}, ui->help ? kBrand : hovered ? kButtonHover : kButton, 3.f);
        if (!ui->help)
            strokeRect(vg, {{0, 0}, box.size}, kTextDim, 1.f, 3.f);
        setFont(vg);
        centered(vg, {{0, 0}, box.size}, "? HELP", ui->help ? kTextDark : kInk, 10.f);
    }

    void onButton(const ButtonEvent& e) override
    {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            ui->help = !ui->help;
            e.consume(this);
        }
    }

    void onEnter(const EnterEvent& e) override
    {
        hovered = true;
        ui->hint = "Every gesture and shortcut (key H)";
    }

    void onLeave(const LeaveEvent& e) override { hovered = false; }
};

} // namespace

struct TetradWidget : ModuleWidget {
    UiState ui;
    GridPanel* grid = nullptr;

    Tetrad* tetrad = nullptr;   // null in the module browser

    explicit TetradWidget(Tetrad* module) : tetrad(module)
    {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Tetrad.svg")));

        auto* inspector = new Inspector;
        inspector->module = module;
        inspector->ui = &ui;
        inspector->box.pos = Vec(8, 26);
        addChild(inspector);

        grid = new GridPanel;
        grid->module = module;
        grid->ui = &ui;
        grid->box.pos = Vec(244, 26);
        addChild(grid);

        const Vec origin = inspector->box.pos;
        for (int i = 0; i < 2; ++i) {
            const bool play = i == 0;
            auto* b = createParam<TransportButton>(origin.plus((play ? kPlayRect : kResetRect).pos), module,
                                                   play ? Tetrad::PLAY_PARAM : Tetrad::RESET_PARAM);
            b->ui = &ui;
            b->isPlay = play;
            b->momentary = !play;
            addParam(b);
        }
        addChild(createLightCentered<SmallLight<GreenLight>>(origin.plus(kClockLightPos), module, Tetrad::CLOCK_LIGHT));

        auto* labels = new Labels;
        labels->box.size = box.size;
        labels->add(Vec(420, 12), "TETRAD", 12.f, kBrand);
        constexpr float jackY = 352, labelY = 330;
        const char* inputNames[] = {"CLOCK", "RESET", "RUN", "TRANSP"};
        for (int i = 0; i < Tetrad::NUM_INPUTS; ++i) {
            addInput(createInputCentered<PJ301MPort>(Vec(30 + i * 37, jackY), module, i));
            labels->add(Vec(30 + i * 37, labelY), inputNames[i], 7.5f);
        }
        for (int t = 0; t < kNumTracks; ++t) {
            const float x = 206 + t * 158;
            addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(
                Vec(x, jackY), module, trackParamIndex(t, kTrackEnabled), Tetrad::ON_LIGHT + t));
            labels->add(Vec(x, labelY), string::f("T%d", t + 1), 9.f);
            const char* outNames[] = {"V/OCT", "GATE", "VEL"};
            for (int k = 0; k < 3; ++k) {
                const int id = (k == 0 ? Tetrad::VOCT_OUTPUT : k == 1 ? Tetrad::GATE_OUTPUT : Tetrad::VEL_OUTPUT) + t;
                addOutput(createOutputCentered<PJ301MPort>(Vec(x + 36 + k * 34, jackY), module, id));
                labels->add(Vec(x + 36 + k * 34, labelY), outNames[k], 7.f);
            }
        }
        addChild(labels);

        auto* helpButton = new HelpButton;
        helpButton->ui = &ui;
        helpButton->box.pos = Vec(grid->box.pos.x + grid->box.size.x - 60, 3);
        helpButton->box.size = Vec(60, 19);
        addChild(helpButton);

        auto* help = new HelpOverlay;
        help->ui = &ui;
        help->box.pos = Vec(4, 24);
        help->box.size = Vec(box.size.x - 8, 292);
        addChild(help);
    }


    void step() override
    {
        ui.refresh(tetrad);
        GLFWwindow* win = APP->window->win;
        if (heldKey >= 0 && glfwGetKey(win, heldKey) != GLFW_PRESS)
            heldKey = -1;
        // Rack scrolls the view while an arrow key is down; an arrow used by Tetrad keeps it still.
        Vec& offset = APP->scene->rackScroll->offset;
        if (heldKey >= 0 && heldOurs)
            offset = viewOffset;
        else if (glfwGetKey(win, GLFW_KEY_LEFT) != GLFW_PRESS && glfwGetKey(win, GLFW_KEY_RIGHT) != GLFW_PRESS &&
                 glfwGetKey(win, GLFW_KEY_UP) != GLFW_PRESS && glfwGetKey(win, GLFW_KEY_DOWN) != GLFW_PRESS)
            viewOffset = offset;
        ModuleWidget::step();
    }

    // Moves the selected steps (data and selection) by `delta` steps.
    void moveSelection(int delta)
    {
        const int t = ui.view.track;
        const StepMask sel = ui.selection;
        StepMask moved;
        editWithUndo(tetrad, "move steps", [&](PatternBank& b) {
            Clipboard clip;
            copySteps(b, t, sel, clip);
            clearSteps(b, t, sel);
            moved = pasteSteps(b, t, firstStep(sel) + delta, clip);
            return true;
        });
        ui.selection = moved;
    }

    // Tap tempo for the internal clock: the average of the last taps, reset after a 2 s pause.
    static constexpr int kTaps = 5;
    double taps[kTaps] = {};
    int tapCount = 0;

    void tapTempo()
    {
        Tetrad* m = tetrad;
        if (m->externalClock.load(std::memory_order_relaxed))
            return;
        const double now = system::getTime();
        if (tapCount > 0 && now - taps[(tapCount - 1) % kTaps] > 2.0)
            tapCount = 0;
        taps[tapCount % kTaps] = now;
        ++tapCount;
        const int n = std::min(tapCount, kTaps);
        if (n < 2)
            return;
        const double span = now - taps[(tapCount - n) % kTaps];
        const float bpm = clamp(float(60.0 * (n - 1) / span), 20.f, 300.f);
        setParamWithUndo(m, Tetrad::TEMPO_PARAM, std::round(bpm * 10.f) / 10.f);
    }

    bool handleKey(const HoverKeyEvent& e)
    {
        Tetrad* m = tetrad;
        const int mods = e.mods & RACK_MOD_MASK;
        const bool ctrl = mods & RACK_MOD_CTRL, shift = mods & GLFW_MOD_SHIFT;
        const bool sel = ui.selection.any();
        const int t = ui.view.track;
        const std::string k = e.keyName;

        // Keys found by position (digits, arrows...), whatever the keyboard layout.
        if (!ctrl && e.key >= GLFW_KEY_1 && e.key <= GLFW_KEY_4) {
            ui.selectTrack(e.key - GLFW_KEY_1);
            return true;
        }
        switch (e.key) {
        case GLFW_KEY_SPACE:
            setParamWithUndo(m, Tetrad::PLAY_PARAM, m->params[Tetrad::PLAY_PARAM].getValue() > 0.5f ? 0.f : 1.f);
            return true;
        case GLFW_KEY_ESCAPE:
            if (!sel && !ui.help)
                return false;
            ui.selection.reset();
            ui.help = false;
            return true;
        case GLFW_KEY_DELETE:
        case GLFW_KEY_BACKSPACE:
            if (!sel)
                return false;   // Rack deletes the module
            editWithUndo(m, "clear steps", [&](PatternBank& b) {
                clearSteps(b, t, ui.selection);
                return true;
            });
            return true;
        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_PAGE_DOWN:
            ui.view.page = clamp(ui.view.page + (e.key == GLFW_KEY_PAGE_DOWN ? 1 : -1), 0, kGridPages - 1);
            ui.follow = false;
            return true;
        case GLFW_KEY_LEFT:
        case GLFW_KEY_RIGHT: {
            const int d = e.key == GLFW_KEY_RIGHT ? 1 : -1;
            if (sel && !shift) {
                moveSelection(d);
            } else if (shift) {
                rotateLoop(m, t, d);
            } else {
                ui.view.page = clamp(ui.view.page + d, 0, kGridPages - 1);
                ui.follow = false;
            }
            return true;
        }
        case GLFW_KEY_UP:
        case GLFW_KEY_DOWN: {
            const int d = e.key == GLFW_KEY_UP ? 1 : -1;
            if (sel && ui.view.lane == kLaneNote) {
                const int amount = d * (shift ? scaleSize(int(std::round(m->params[globalParamIndex(kGlobalScale)].getValue()))) : 1);
                editWithUndo(m, "transpose steps", [&](PatternBank& b) { return transposeSteps(b, t, ui.selection, amount); });
            } else if (sel) {
                const int amount = d * (shift ? 10 : 1);
                editWithUndo(m, "edit steps",
                             [&](PatternBank& b) { return adjustSteps(b, t, ui.selection, kLaneField[ui.view.lane], amount); });
            } else if (ui.view.lane == kLaneNote) {
                grid->scroll(d * (shift ? 7 : 1));
            } else {
                return false;
            }
            return true;
        }
        default:
            break;
        }

        // Letters by name, so they follow the layout (AZERTY included).
        if (k.size() != 1)
            return false;
        if (ctrl) {
            switch (k[0]) {
            case 'a':
                ui.selection = windowMask(loopStart(m, t), loopLength(m, t));
                return true;
            case 'c':
            case 'x':
                if (!sel)
                    return false;   // Rack copies the module preset
                ui.refresh(m);
                copySteps(ui.bank, t, ui.selection, ui.clipboard);
                if (k[0] == 'x')
                    editWithUndo(m, "cut steps", [&](PatternBank& b) {
                        clearSteps(b, t, ui.selection);
                        return true;
                    });
                return true;
            case 'v': {
                if (ui.clipboard.count == 0)
                    return false;
                const int at = sel ? firstStep(ui.selection) : (ui.anchor >= 0 ? ui.anchor : ui.view.page * kGridCols);
                StepMask written;
                editWithUndo(m, "paste steps", [&](PatternBank& b) {
                    written = pasteSteps(b, t, at, ui.clipboard);
                    return written.any();
                });
                ui.selection = written;
                return true;
            }
            case 'd':
                duplicateLoop(m, t);
                return true;
            default:
                return false;
            }
        }
        if (shift)
            return false;
        switch (k[0]) {
        case 'n': ui.view.lane = kLaneNote; return true;
        case 'v': ui.view.lane = kLaneVelocity; return true;
        case 'g': ui.view.lane = kLaneGate; return true;
        case 'p': ui.view.lane = kLaneProbability; return true;
        case 'r': ui.view.lane = kLaneRatchet; return true;
        case 'c': ui.view.lane = kLaneCondition; return true;
        case 'f': ui.follow = !ui.follow; return true;
        case 'h': ui.help = !ui.help; return true;
        case 'm': {
            const int id = trackParamIndex(t, kTrackEnabled);
            setParamWithUndo(m, id, m->params[id].getValue() > 0.5f ? 0.f : 1.f);
            return true;
        }
        case 't': tapTempo(); return true;
        case 'e': euclidLoop(m, ui); return true;
        case 's': newSeed(m, t); return true;
        default: return false;
        }
    }

    // Keys that are always ours (plain letters, digits, Space), whatever the state.
    static bool handlesKey(const HoverKeyEvent& e)
    {
        if (e.mods & RACK_MOD_MASK)
            return false;
        if (e.key == GLFW_KEY_SPACE || (e.key >= GLFW_KEY_1 && e.key <= GLFW_KEY_4))
            return true;
        return e.keyName.size() == 1 && std::strchr("nvgprcfhmest", e.keyName[0]);
    }

    static bool isNavigation(int key)
    {
        return key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT || key == GLFW_KEY_UP || key == GLFW_KEY_DOWN ||
               key == GLFW_KEY_PAGE_UP || key == GLFW_KEY_PAGE_DOWN;
    }

    // Rack keeps the press of arrow keys (it scrolls the rack with them) and then sends RACK_HELD
    // every frame while the key is down: the first one acts as the press, later ones auto-repeat.
    // step() notices the release.
    int heldKey = -1;
    Vec viewOffset;   // rack scroll before the arrow key went down
    bool heldOurs = false;
    double nextRepeat = 0.0;

    // Our keys first: Rack's own module keys (Delete, Ctrl+C...) only get what we leave.
    void onHoverKey(const HoverKeyEvent& e) override
    {
        if (!module)
            return ModuleWidget::onHoverKey(e);
        if (isNavigation(e.key) && e.action != GLFW_RELEASE) {
            const double now = system::getTime();
            if (e.action == GLFW_PRESS || heldKey != e.key) {
                heldKey = e.key;
                nextRepeat = now + 0.4;
                heldOurs = handleKey(e);
            } else if (e.action == RACK_HELD && heldOurs && now >= nextRepeat) {
                nextRepeat = now + 0.06;
                handleKey(e);
            }
            if (heldOurs)
                e.consume(this);
            else
                ModuleWidget::onHoverKey(e);
            return;
        }
        if ((e.action == GLFW_PRESS && handleKey(e)) || (e.action == GLFW_REPEAT && handlesKey(e))) {
            e.consume(this);   // a held letter (H, M, Space...) acts once
            return;
        }
        ModuleWidget::onHoverKey(e);
    }

    void appendContextMenu(Menu* menu) override
    {
        auto* m = tetrad;
        if (!m)
            return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Gestures and shortcuts", "H", [=] { ui.help = true; }));
        appendLoopTools(menu, m, &ui);
        std::vector<std::string> ppqnLabels;
        for (int p : kPpqnChoices)
            ppqnLabels.push_back(string::f("%d", p));
        menu->addChild(createIndexSubmenuItem("Clock PPQN (input, or Pipe clock output)", ppqnLabels,
                                                [=] { return size_t(m->ppqnIndex.load()); },
                                                [=](size_t i) { m->ppqnIndex.store(int(i)); }));
        auto* tempo = new ui::Slider;
        tempo->quantity = m->getParamQuantity(Tetrad::TEMPO_PARAM);
        tempo->box.size.x = 220.f;
        menu->addChild(tempo);
    }
};

} // namespace ui_tetrad

Model* modelTetrad = createModel<Tetrad, ui_tetrad::TetradWidget>("Tetrad");

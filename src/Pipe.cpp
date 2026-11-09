// Tetrad Pipe: routing expander for Tetrad (place it directly on Tetrad's right).
// 16 identical outputs. Right-click an output to choose its signal (pitch, velocity, gate,
// trigger, one grid row's gate / trigger / velocity, clock, reset), the track and the row.
// A Poly track becomes a drum track this way: each grid row (scale degree 0..7) is a lane with
// its own gate, trigger and velocity.

#include "branding.hpp"
#include "pipe.hpp"
#include "plugin.hpp"

using namespace rack;

namespace tetrad::pipe {

const char* const kSignalNames[kSignalCount] = {
    "None",      "Pitch (V/oct, poly)", "Velocity (poly)", "Gate (poly)", "Trigger",
    "Row gate",  "Row trigger",         "Row velocity",    "Clock",       "Reset",
};

// The short name leaves the track out: the panel shows it as a coloured tag.
std::string routeName(const Route& r, bool shortName)
{
    const int t = r.track + 1, row = r.row + 1;
    switch (r.signal) {
    case kPitch: return shortName ? "PITCH" : string::f("Track %d pitch", t);
    case kVelocity: return shortName ? "VEL" : string::f("Track %d velocity", t);
    case kGate: return shortName ? "GATE" : string::f("Track %d gate", t);
    case kTrigger: return shortName ? "TRIG" : string::f("Track %d trigger", t);
    case kRowGate: return string::f(shortName ? "R%d GATE" : "Track %d row %d gate", shortName ? row : t, row);
    case kRowTrigger: return string::f(shortName ? "R%d TRIG" : "Track %d row %d trigger", shortName ? row : t, row);
    case kRowVelocity: return string::f(shortName ? "R%d VEL" : "Track %d row %d velocity", shortName ? row : t, row);
    case kClock: return shortName ? "CLOCK" : "Clock";
    case kReset: return shortName ? "RESET" : "Reset";
    default: return shortName ? "-" : "None";
    }
}

// 1-4 pitch, 5-8 velocity, 9-12 gate of tracks 1-4; 13-14 triggers of tracks 1-2; clock, reset.
Route defaultRoute(int i)
{
    Route r;
    r.track = i % kTracks;
    if (i < 4)
        r.signal = kPitch;
    else if (i < 8)
        r.signal = kVelocity;
    else if (i < 12)
        r.signal = kGate;
    else if (i < 14)
        r.signal = kTrigger;
    else
        r.signal = i == 14 ? kClock : kReset;
    return r;
}

} // namespace tetrad::pipe

struct TetradPipe : Module {
    enum OutputId { ENUMS(ROUTED_OUTPUT, tetrad::pipe::kOutputs), NUM_OUTPUTS };
    enum LightId { LINK_LIGHT, NUM_LIGHTS };

    PipeMessage messages[2] = {};
    uint32_t lastSerial = 0;
    tetrad::pipe::Route routes[tetrad::pipe::kOutputs];

    TetradPipe()
    {
        config(0, 0, NUM_OUTPUTS, NUM_LIGHTS);
        for (int i = 0; i < tetrad::pipe::kOutputs; ++i)
            configOutput(ROUTED_OUTPUT + i, string::f("Routed %d", i + 1))->description = "Right-click to choose its signal, track and row";
        configLight(LINK_LIGHT, "Linked to Tetrad (place Pipe directly on its right)");
        leftExpander.producerMessage = &messages[0];
        leftExpander.consumerMessage = &messages[1];
        defaultRouting();
    }

    void defaultRouting()
    {
        for (int i = 0; i < tetrad::pipe::kOutputs; ++i)
            routes[i] = tetrad::pipe::defaultRoute(i);
    }

    // A whole track as drums: outputs 1-8 = velocity of rows 1-8, 9-16 = gate of rows 1-8.
    void drumRouting(int track)
    {
        for (int i = 0; i < tetrad::pipe::kOutputs; ++i)
            routes[i] = {i < 8 ? tetrad::pipe::kRowVelocity : tetrad::pipe::kRowGate, track, i % 8};
    }

    void onReset(const ResetEvent& e) override
    {
        Module::onReset(e);
        defaultRouting();
    }

    void process(const ProcessArgs& args) override
    {
        const PipeMessage& m = *static_cast<const PipeMessage*>(leftExpander.consumerMessage);
        // Linked: Tetrad on the left and sending (a bypassed Tetrad sends nothing new).
        const bool fresh = m.serial != lastSerial;
        lastSerial = m.serial;
        const bool linked = fresh && leftExpander.module && leftExpander.module->model == modelTetrad;
        lights[LINK_LIGHT].setBrightness(linked ? 1.f : 0.f);
        const tetrad::CvFrame& f = m.frame;

        for (int i = 0; i < tetrad::pipe::kOutputs; ++i) {
            Output& out = outputs[ROUTED_OUTPUT + i];
            if (!out.isConnected())
                continue;
            const tetrad::pipe::Route r = routes[i];
            const int t = clamp(r.track, 0, tetrad::pipe::kTracks - 1);
            const int row = clamp(r.row, 0, tetrad::pipe::kRows - 1);
            if (!linked) {
                out.setVoltage(0.f);
                out.setChannels(1);
                continue;
            }
            switch (r.signal) {
            case tetrad::pipe::kPitch:
            case tetrad::pipe::kVelocity:
            case tetrad::pipe::kGate: {
                const int n = f.channels[t];
                for (int c = 0; c < n; ++c) {
                    float v;
                    if (r.signal == tetrad::pipe::kPitch)
                        v = (f.key[t][c] - 60) / 12.f;
                    else if (r.signal == tetrad::pipe::kVelocity)
                        v = f.velocity[t][c] * (10.f / 127.f);
                    else
                        v = (f.gateMask[t] >> c) & 1u ? 10.f : 0.f;
                    out.setVoltage(v, c);
                }
                out.setChannels(n);
                continue;
            }
            case tetrad::pipe::kTrigger: out.setVoltage((f.trigMask >> t) & 1u ? 10.f : 0.f); break;
            case tetrad::pipe::kRowGate: out.setVoltage((f.rowGate[t] >> row) & 1u ? 10.f : 0.f); break;
            case tetrad::pipe::kRowTrigger: out.setVoltage((f.rowTrig[t] >> row) & 1u ? 10.f : 0.f); break;
            case tetrad::pipe::kRowVelocity: out.setVoltage(f.rowVelocity[t][row] * (10.f / 127.f)); break;
            case tetrad::pipe::kClock: out.setVoltage(m.clock ? 10.f : 0.f); break;
            case tetrad::pipe::kReset: out.setVoltage(m.reset ? 10.f : 0.f); break;
            default: out.setVoltage(0.f); break;
            }
            out.setChannels(1);
        }
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_t* list = json_array();
        for (const tetrad::pipe::Route& r : routes) {
            json_t* o = json_object();
            json_object_set_new(o, "signal", json_integer(r.signal));
            json_object_set_new(o, "track", json_integer(r.track));
            json_object_set_new(o, "row", json_integer(r.row));
            json_array_append_new(list, o);
        }
        json_object_set_new(root, "routes", list);
        return root;
    }

    void dataFromJson(json_t* root) override
    {
        json_t* list = json_object_get(root, "routes");
        for (int i = 0; i < tetrad::pipe::kOutputs; ++i) {
            json_t* o = json_array_get(list, i);
            if (!o)
                continue;
            tetrad::pipe::Route& r = routes[i];
            r.signal = clamp(int(json_integer_value(json_object_get(o, "signal"))), 0, tetrad::pipe::kSignalCount - 1);
            r.track = clamp(int(json_integer_value(json_object_get(o, "track"))), 0, tetrad::pipe::kTracks - 1);
            r.row = clamp(int(json_integer_value(json_object_get(o, "row"))), 0, tetrad::pipe::kRows - 1);
        }
    }
};

namespace {

// Route edits from the menus, one undo entry each.
struct RoutesChange : history::ModuleAction {
    tetrad::pipe::Route before[tetrad::pipe::kOutputs], after[tetrad::pipe::kOutputs];
    void set(const tetrad::pipe::Route* routes)
    {
        if (auto* m = dynamic_cast<TetradPipe*>(APP->engine->getModule(moduleId)))
            std::copy(routes, routes + tetrad::pipe::kOutputs, m->routes);
    }
    void undo() override { set(before); }
    void redo() override { set(after); }
};

bool sameRoute(const tetrad::pipe::Route& a, const tetrad::pipe::Route& b)
{
    return a.signal == b.signal && a.track == b.track && a.row == b.row;
}

template <typename F>
void editRoutes(TetradPipe* m, const std::string& name, F&& fn)
{
    auto* a = new RoutesChange;
    a->moduleId = m->id;
    a->name = name;
    std::copy(m->routes, m->routes + tetrad::pipe::kOutputs, a->before);
    fn();
    std::copy(m->routes, m->routes + tetrad::pipe::kOutputs, a->after);
    if (std::equal(a->before, a->before + tetrad::pipe::kOutputs, a->after, sameRoute))
        delete a;
    else
        APP->history->push(a);
}

const NVGcolor kNumber = nvgRGBA(23, 24, 28, 170);   // on the amber columns
const NVGcolor kTagInk = nvgRGB(23, 24, 28);
// Track colours, the same as Tetrad's track tabs (ui.hpp).
const NVGcolor kTrack[tetrad::pipe::kTracks] = {nvgRGB(255, 176, 59), nvgRGB(46, 196, 182), nvgRGB(170, 120, 240),
                                       nvgRGB(241, 91, 181)};

// Panel layout (Rack pixels, 10 HP = 150 px wide): two amber columns of 70 px under the name
// band (every jack is an output: VCV's inverted background), outputs 1-8 left, 9-16 right.
// Each cell: the number at the top left, the jack, and under it the route as a dark tag with
// the track number in the track's colour, then the signal.
constexpr float kLeftX = 39, kRightX = 111, kRowY = 52, kRowStep = 40;
const math::Rect kColumns[2] = {{{4, 32}, {70, 328}}, {{76, 32}, {70, 328}}};
Vec jackPos(int i) { return Vec(i < 8 ? kLeftX : kRightX, kRowY + (i % 8) * kRowStep); }

// Right-click menu of an output: signal, track and row.
struct RoutedPort : PJ301MPort {
    TetradPipe* pipeModule = nullptr;
    int index = 0;

    void appendContextMenu(Menu* menu) override
    {
        TetradPipe* m = pipeModule;
        if (!m)
            return;
        const int i = index;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel(string::f("Output %d: %s", i + 1, tetrad::pipe::routeName(m->routes[i], false).c_str())));
        menu->addChild(createSubmenuItem("Signal", tetrad::pipe::kSignalNames[m->routes[i].signal], [=](Menu* sub) {
            for (int s = 0; s < tetrad::pipe::kSignalCount; ++s)
                sub->addChild(createCheckMenuItem(tetrad::pipe::kSignalNames[s], "", [=] { return m->routes[i].signal == s; },
                                                  [=] { editRoutes(m, "route output", [&] { m->routes[i].signal = s; }); }));
        }));
        menu->addChild(createSubmenuItem(
            "Track", string::f("%d", m->routes[i].track + 1),
            [=](Menu* sub) {
                for (int t = 0; t < tetrad::pipe::kTracks; ++t)
                    sub->addChild(createCheckMenuItem(string::f("Track %d", t + 1), "",
                                                      [=] { return m->routes[i].track == t; },
                                                      [=] { editRoutes(m, "route output", [&] { m->routes[i].track = t; }); }));
            },
            !tetrad::pipe::usesTrack(m->routes[i].signal)));
        menu->addChild(createSubmenuItem(
            "Row", string::f("%d", m->routes[i].row + 1),
            [=](Menu* sub) {
                for (int r = tetrad::pipe::kRows - 1; r >= 0; --r)   // top row first, like the grid
                    sub->addChild(createCheckMenuItem(string::f("Row %d%s", r + 1, r == 0 ? " (bottom)" : ""), "",
                                                      [=] { return m->routes[i].row == r; },
                                                      [=] { editRoutes(m, "route output", [&] { m->routes[i].row = r; }); }));
            },
            !tetrad::pipe::usesRow(m->routes[i].signal)));
    }
};

// Route under each jack, so the panel always shows where things go: a dark tag with the track
// number in the track's colour, then the signal; clock and reset have no tag.
struct RoutingLabels : TransparentWidget {
    TetradPipe* module = nullptr;

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if (layer != 1)
            return TransparentWidget::drawLayer(args, layer);
        auto font = panelFont();
        if (!font)
            return;
        NVGcontext* vg = args.vg;
        nvgFontFaceId(vg, font->handle);
        constexpr float kTagW = 13.f, kTagH = 11.f, kGap = 3.f, kSize = 9.5f;
        for (int i = 0; i < tetrad::pipe::kOutputs; ++i) {
            const tetrad::pipe::Route r = module ? module->routes[i] : tetrad::pipe::defaultRoute(i);
            const Vec p = jackPos(i);
            const float y = p.y + 20.f;
            const std::string name = tetrad::pipe::routeName(r, true);
            const bool tagged = tetrad::pipe::usesTrack(r.signal);
            nvgFontSize(vg, kSize);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            float bounds[4];
            const float textW = nvgTextBounds(vg, 0, 0, name.c_str(), nullptr, bounds);
            float x = p.x - (textW + (tagged ? kTagW + kGap : 0.f)) / 2.f;
            if (tagged) {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x, y - kTagH / 2.f, kTagW, kTagH, 2.f);
                nvgFillColor(vg, kTagInk);
                nvgFill(vg);
                nvgFontSize(vg, 8.f);
                nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgFillColor(vg, kTrack[r.track]);
                nvgText(vg, x + kTagW / 2.f, y + 0.5f, string::f("%d", r.track + 1).c_str(), nullptr);
                nvgFontSize(vg, kSize);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                x += kTagW + kGap;
            }
            nvgFillColor(vg, kTagInk);
            nvgText(vg, x, y, name.c_str(), nullptr);
        }
    }
};

struct StaticLabels : TransparentWidget {
    void draw(const DrawArgs& args) override
    {
        auto font = panelFont();
        if (!font)
            return;
        NVGcontext* vg = args.vg;
        nvgFontFaceId(vg, font->handle);
        // Output numbers, small, at the top left of each jack.
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, kNumber);
        nvgFontSize(vg, 8.f);
        for (int i = 0; i < tetrad::pipe::kOutputs; ++i) {
            const Vec p = jackPos(i);
            nvgText(vg, p.x - 15, p.y - 9, string::f("%d", i + 1).c_str(), nullptr);
        }
    }
};

} // namespace

struct TetradPipeWidget : ModuleWidget {
    explicit TetradPipeWidget(TetradPipe* module)
    {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/TetradPipe.svg")));
        for (const math::Rect& c : kColumns) {
            auto* tab = new OutputTab;
            tab->box = c;
            addChild(tab);
        }
        auto* labels = new StaticLabels;
        labels->box.size = box.size;
        addChild(labels);
        for (int i = 0; i < tetrad::pipe::kOutputs; ++i) {
            auto* port = createOutputCentered<RoutedPort>(jackPos(i), module, TetradPipe::ROUTED_OUTPUT + i);
            port->pipeModule = module;
            port->index = i;
            addOutput(port);
        }
        // Bottom right, beside the lockup.
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(box.size.x - 14, box.size.y - 10), module, TetradPipe::LINK_LIGHT));
        auto* branding = new Branding("res/brand/tetrad-pipe.svg", "PIPE", "ROUTER");
        branding->box.size = box.size;
        addChild(branding);
        auto* routing = new RoutingLabels;
        routing->module = module;
        routing->box.size = box.size;
        addChild(routing);
    }

    void appendContextMenu(Menu* menu) override
    {
        auto* m = dynamic_cast<TetradPipe*>(module);
        if (!m)
            return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Right-click an output to route it"));
        menu->addChild(createMenuItem("Default routing", "", [=] { editRoutes(m, "default routing", [&] { m->defaultRouting(); }); }));
        menu->addChild(createSubmenuItem("Route a track as drums (rows 1-8)", "", [=](Menu* sub) {
            for (int t = 0; t < tetrad::pipe::kTracks; ++t)
                sub->addChild(createMenuItem(string::f("Track %d", t + 1), "1-8 velocity, 9-16 gate",
                                             [=] { editRoutes(m, "drum routing", [&] { m->drumRouting(t); }); }));
        }));
    }
};

Model* modelTetradPipe = createModel<TetradPipe, TetradPipeWidget>("TetradPipe");

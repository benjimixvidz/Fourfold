// Tetrad Pipe: routing expander for Tetrad (place it directly on Tetrad's right).
// 16 identical outputs. Right-click an output to choose its signal (pitch, velocity, gate,
// trigger, one grid row's gate / trigger / velocity, clock, reset), the track and the row.
// A Poly track becomes a drum track this way: each grid row (scale degree 0..7) is a lane with
// its own gate, trigger and velocity.

#include "branding.hpp"
#include "pipe.hpp"
#include "plugin.hpp"

using namespace rack;

namespace pipe {

const char* const kSignalNames[kSignalCount] = {
    "None",      "Pitch (V/oct, poly)", "Velocity (poly)", "Gate (poly)", "Trigger",
    "Row gate",  "Row trigger",         "Row velocity",    "Clock",       "Reset",
};

std::string routeName(const Route& r, bool shortName)
{
    const int t = r.track + 1, row = r.row + 1;
    switch (r.signal) {
    case kPitch: return string::f(shortName ? "T%d PIT" : "Track %d pitch", t);
    case kVelocity: return string::f(shortName ? "T%d VEL" : "Track %d velocity", t);
    case kGate: return string::f(shortName ? "T%d GATE" : "Track %d gate", t);
    case kTrigger: return string::f(shortName ? "T%d TRIG" : "Track %d trigger", t);
    case kRowGate: return string::f(shortName ? "T%dR%d G" : "Track %d row %d gate", t, row);
    case kRowTrigger: return string::f(shortName ? "T%dR%d T" : "Track %d row %d trigger", t, row);
    case kRowVelocity: return string::f(shortName ? "T%dR%d V" : "Track %d row %d velocity", t, row);
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

} // namespace pipe

struct TetradPipe : Module {
    enum OutputId { ENUMS(ROUTED_OUTPUT, pipe::kOutputs), NUM_OUTPUTS };
    enum LightId { LINK_LIGHT, NUM_LIGHTS };

    PipeMessage messages[2] = {};
    pipe::Route routes[pipe::kOutputs];

    TetradPipe()
    {
        config(0, 0, NUM_OUTPUTS, NUM_LIGHTS);
        for (int i = 0; i < pipe::kOutputs; ++i)
            configOutput(ROUTED_OUTPUT + i, string::f("Output %d (right-click to route)", i + 1));
        configLight(LINK_LIGHT, "Linked to Tetrad (place Pipe directly on its right)");
        leftExpander.producerMessage = &messages[0];
        leftExpander.consumerMessage = &messages[1];
        defaultRouting();
    }

    void defaultRouting()
    {
        for (int i = 0; i < pipe::kOutputs; ++i)
            routes[i] = pipe::defaultRoute(i);
    }

    // A whole track as drums: outputs 1-8 = velocity of rows 1-8, 9-16 = gate of rows 1-8.
    void drumRouting(int track)
    {
        for (int i = 0; i < pipe::kOutputs; ++i)
            routes[i] = {i < 8 ? pipe::kRowVelocity : pipe::kRowGate, track, i % 8};
    }

    void onReset(const ResetEvent& e) override
    {
        Module::onReset(e);
        defaultRouting();
    }

    void process(const ProcessArgs& args) override
    {
        const bool linked = leftExpander.module && leftExpander.module->model == modelTetrad;
        lights[LINK_LIGHT].setBrightness(linked ? 1.f : 0.f);
        const PipeMessage& m = *static_cast<const PipeMessage*>(leftExpander.consumerMessage);
        const tetrad::CvFrame& f = m.frame;

        for (int i = 0; i < pipe::kOutputs; ++i) {
            Output& out = outputs[ROUTED_OUTPUT + i];
            if (!out.isConnected())
                continue;
            const pipe::Route r = routes[i];
            const int t = clamp(r.track, 0, pipe::kTracks - 1);
            const int row = clamp(r.row, 0, pipe::kRows - 1);
            if (!linked) {
                out.setVoltage(0.f);
                out.setChannels(1);
                continue;
            }
            switch (r.signal) {
            case pipe::kPitch:
            case pipe::kVelocity:
            case pipe::kGate: {
                const int n = f.channels[t];
                for (int c = 0; c < n; ++c) {
                    float v;
                    if (r.signal == pipe::kPitch)
                        v = (f.key[t][c] - 60) / 12.f;
                    else if (r.signal == pipe::kVelocity)
                        v = f.velocity[t][c] * (10.f / 127.f);
                    else
                        v = (f.gateMask[t] >> c) & 1u ? 10.f : 0.f;
                    out.setVoltage(v, c);
                }
                out.setChannels(n);
                continue;
            }
            case pipe::kTrigger: out.setVoltage((f.trigMask >> t) & 1u ? 10.f : 0.f); break;
            case pipe::kRowGate: out.setVoltage((f.laneGate[t] >> row) & 1u ? 10.f : 0.f); break;
            case pipe::kRowTrigger: out.setVoltage((f.laneTrig[t] >> row) & 1u ? 10.f : 0.f); break;
            case pipe::kRowVelocity: out.setVoltage(f.laneVelocity[t][row] * (10.f / 127.f)); break;
            case pipe::kClock: out.setVoltage(m.clock ? 10.f : 0.f); break;
            case pipe::kReset: out.setVoltage(m.reset ? 10.f : 0.f); break;
            default: out.setVoltage(0.f); break;
            }
            out.setChannels(1);
        }
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_t* list = json_array();
        for (const pipe::Route& r : routes) {
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
        for (int i = 0; i < pipe::kOutputs; ++i) {
            json_t* o = json_array_get(list, i);
            if (!o)
                continue;
            pipe::Route& r = routes[i];
            r.signal = clamp(int(json_integer_value(json_object_get(o, "signal"))), 0, pipe::kSignalCount - 1);
            r.track = clamp(int(json_integer_value(json_object_get(o, "track"))), 0, pipe::kTracks - 1);
            r.row = clamp(int(json_integer_value(json_object_get(o, "row"))), 0, pipe::kRows - 1);
        }
    }
};

namespace {

const NVGcolor kLabel = nvgRGB(201, 206, 214);
const NVGcolor kRoute = nvgRGB(255, 176, 59);   // brand amber

// Panel layout (Rack pixels, 12 HP = 180 px wide): outputs 1-8 left, 9-16 right.
constexpr float kLeftX = 26 + kStripWidth, kRightX = 116 + kStripWidth - 12, kRowY = 64, kRowStep = 36;
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
        menu->addChild(createMenuLabel(string::f("Output %d: %s", i + 1, pipe::routeName(m->routes[i], false).c_str())));
        menu->addChild(createSubmenuItem("Signal", pipe::kSignalNames[m->routes[i].signal], [=](Menu* sub) {
            for (int s = 0; s < pipe::kSignalCount; ++s)
                sub->addChild(createCheckMenuItem(pipe::kSignalNames[s], "", [=] { return m->routes[i].signal == s; },
                                                  [=] { m->routes[i].signal = s; }));
        }));
        menu->addChild(createSubmenuItem(
            "Track", string::f("%d", m->routes[i].track + 1),
            [=](Menu* sub) {
                for (int t = 0; t < pipe::kTracks; ++t)
                    sub->addChild(createCheckMenuItem(string::f("Track %d", t + 1), "",
                                                      [=] { return m->routes[i].track == t; },
                                                      [=] { m->routes[i].track = t; }));
            },
            !pipe::usesTrack(m->routes[i].signal)));
        menu->addChild(createSubmenuItem(
            "Row", string::f("%d", m->routes[i].row + 1),
            [=](Menu* sub) {
                for (int r = pipe::kRows - 1; r >= 0; --r)   // top row first, like the grid
                    sub->addChild(createCheckMenuItem(string::f("Row %d%s", r + 1, r == 0 ? " (bottom)" : ""), "",
                                                      [=] { return m->routes[i].row == r; },
                                                      [=] { m->routes[i].row = r; }));
            },
            !pipe::usesRow(m->routes[i].signal)));
    }
};

// Routing labels next to each jack, so the panel always shows where things go.
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
        nvgFontSize(vg, 8.5f);
        nvgFillColor(vg, kRoute);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        for (int i = 0; i < pipe::kOutputs; ++i) {
            const pipe::Route r = module ? module->routes[i] : pipe::defaultRoute(i);
            const Vec p = jackPos(i);
            nvgText(vg, p.x + 15, p.y, pipe::routeName(r, true).c_str(), nullptr);
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
        nvgFillColor(vg, kLabel);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFontSize(vg, 7.5f);
        nvgText(vg, (180 + kStripWidth) / 2.f, 37, "RIGHT-CLICK TO ROUTE", nullptr);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFontSize(vg, 8.f);
        for (int i = 0; i < pipe::kOutputs; ++i) {
            const Vec p = jackPos(i);
            nvgText(vg, p.x - 13, p.y, string::f("%d", i + 1).c_str(), nullptr);
        }
    }
};

} // namespace

struct TetradPipeWidget : ModuleWidget {
    explicit TetradPipeWidget(TetradPipe* module)
    {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/TetradPipe.svg")));
        auto* labels = new StaticLabels;
        labels->box.size = box.size;
        addChild(labels);
        for (int i = 0; i < pipe::kOutputs; ++i) {
            auto* port = createOutputCentered<RoutedPort>(jackPos(i), module, TetradPipe::ROUTED_OUTPUT + i);
            port->pipeModule = module;
            port->index = i;
            addOutput(port);
        }
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(168, 14), module, TetradPipe::LINK_LIGHT));
        auto* branding = new Branding("res/brand/tetrad-pipe.svg", "PIPE", true);
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
        menu->addChild(createMenuItem("Default routing", "", [=] { m->defaultRouting(); }));
        menu->addChild(createSubmenuItem("Route a track as drums (rows 1-8)", "", [=](Menu* sub) {
            for (int t = 0; t < pipe::kTracks; ++t)
                sub->addChild(createMenuItem(string::f("Track %d", t + 1), "1-8 velocity, 9-16 gate",
                                             [=] { m->drumRouting(t); }));
        }));
    }
};

Model* modelTetradPipe = createModel<TetradPipe, TetradPipeWidget>("TetradPipe");

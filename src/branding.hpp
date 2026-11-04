#pragma once
// Brand pieces shared by both panels: the amber band across the top (module logo on the left,
// name centred, kind of module on the right, in dark on amber), the Fourfold lockup at the bottom
// centre (res/brand), and the amber tab that marks outputs.

#include "plugin.hpp"

constexpr float kBandTop = 4.f, kBandHeight = 24.f;

struct Branding : rack::TransparentWidget {
    std::shared_ptr<rack::window::Svg> logo, lockup;
    std::string name, kind;
    float kindRight = 0.f;   // right edge of the kind text; 0: the band's right end

    Branding(const std::string& logoFile, const std::string& moduleName, const std::string& moduleKind)
        : name(moduleName), kind(moduleKind)
    {
        logo = rack::window::Svg::load(rack::asset::plugin(pluginInstance, logoFile));
        lockup = rack::window::Svg::load(rack::asset::plugin(pluginInstance, "res/brand/fourfold-lockup.svg"));
    }

    static void drawSvg(NVGcontext* vg, const rack::window::Svg& svg, float x, float y, float scale)
    {
        nvgSave(vg);
        nvgTranslate(vg, x, y);
        nvgScale(vg, scale, scale);
        rack::window::svgDraw(vg, svg.handle);
        nvgRestore(vg);
    }

    void draw(const DrawArgs& args) override
    {
        auto font = panelFont();
        if (!font || !logo || !lockup)
            return;
        NVGcontext* vg = args.vg;
        const float cy = kBandTop + kBandHeight / 2.f, logoSize = 18.f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 4.f, kBandTop, box.size.x - 8.f, kBandHeight, 5.f);
        nvgFillColor(vg, nvgRGB(255, 176, 59));
        nvgFill(vg);
        drawSvg(vg, *logo, 9.f, cy - logoSize / 2.f, logoSize / 64.f);
        nvgFontFaceId(vg, font->handle);
        nvgFillColor(vg, nvgRGB(23, 24, 28));
        nvgFontSize(vg, 17.f);
        nvgTextLetterSpacing(vg, 1.5f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, box.size.x / 2.f, cy + 1.f, name.c_str(), nullptr);
        nvgFontSize(vg, 9.f);
        nvgTextLetterSpacing(vg, 0.5f);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, kindRight > 0.f ? kindRight : box.size.x - 10.f, cy + 0.5f, kind.c_str(), nullptr);
        nvgTextLetterSpacing(vg, 0.f);
        // Lockup 461.8 x 88 (res/brand/fourfold-lockup.svg), 12 px high, 4 px above the bottom.
        const float h = 12.f, w = h * 461.8f / 88.f;
        drawSvg(vg, *lockup, (box.size.x - w) / 2.f, box.size.y - h - 4.f, h / 88.f);
    }
};

// Amber tab under outputs (VCV: an inverted background for output ports). Added before the
// ports so they sit on it.
struct OutputTab : rack::TransparentWidget {
    void draw(const DrawArgs& args) override
    {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 5.f);
        nvgFillColor(args.vg, nvgRGB(255, 176, 59));
        nvgFill(args.vg);
    }
};

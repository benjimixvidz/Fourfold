#pragma once
// The module's logo and name centred at the top of the panel, the Fourfold lockup at the bottom
// left (res/brand).

#include "plugin.hpp"

struct Branding : rack::TransparentWidget {
    std::shared_ptr<rack::window::Svg> logo, lockup;
    std::string name;

    Branding(const std::string& logoFile, const std::string& moduleName) : name(moduleName)
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
        nvgFontFaceId(vg, font->handle);
        nvgFontSize(vg, 12.f);
        float bounds[4];
        const float textWidth = nvgTextBounds(vg, 0, 0, name.c_str(), nullptr, bounds);
        const float icon = 16.f, gap = 5.f;
        const float x = (box.size.x - icon - gap - textWidth) / 2;
        drawSvg(vg, *logo, x, 3.f, icon / 64.f);
        nvgFillColor(vg, nvgRGB(255, 176, 59));   // brand amber
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, x + icon + gap, 11.5f, name.c_str(), nullptr);
        const float height = 12.f;   // the lockup is 462 x 88
        drawSvg(vg, *lockup, 6.f, box.size.y - height - 3.f, height / 88.f);
    }
};

#pragma once
// A2: the module name runs up the left edge on an amber band, the module logo above it and the
// Fourfold mark below (res/brand). The compact header stays for a module without the band.

#include "plugin.hpp"

constexpr float kStripWidth = 46.f;

struct Branding : rack::TransparentWidget {
    std::shared_ptr<rack::window::Svg> logo, mark;
    std::string name;
    bool vertical = false;   // A2 strip on the left edge; else a compact header

    Branding(const std::string& logoFile, const std::string& moduleName, bool strip = false)
        : name(moduleName), vertical(strip)
    {
        logo = rack::window::Svg::load(rack::asset::plugin(pluginInstance, logoFile));
        mark = rack::window::Svg::load(rack::asset::plugin(pluginInstance, "res/brand/fourfold.svg"));
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
        if (!font || !logo || !mark)
            return;
        NVGcontext* vg = args.vg;
        nvgFontFaceId(vg, font->handle);
        nvgFillColor(vg, nvgRGB(255, 176, 59));
        if (vertical) {
            // The amber band of the header, turned on its side; logo, name and mark in dark on it.
            const float cx = kStripWidth / 2.f, logoSize = 30.f, markSize = 22.f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, 3.f, 3.f, kStripWidth - 6.f, box.size.y - 6.f, 5.f);
            nvgFillColor(vg, nvgRGB(255, 176, 59));
            nvgFill(vg);
            drawSvg(vg, *logo, cx - logoSize / 2.f, 12.f, logoSize / 64.f);
            drawSvg(vg, *mark, cx - markSize / 2.f, box.size.y - markSize - 12.f, markSize / 64.f);
            nvgFillColor(vg, nvgRGB(23, 24, 28));
            nvgFontSize(vg, 30.f);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgTextLetterSpacing(vg, 2.f);
            const float top = 12.f + logoSize + 10.f, bottom = box.size.y - markSize - 22.f;
            nvgSave(vg);
            nvgTranslate(vg, cx, (top + bottom) / 2.f);
            nvgRotate(vg, -M_PI / 2.f);
            nvgText(vg, 0, 1.f, name.c_str(), nullptr);
            nvgRestore(vg);
            return;
        }
        nvgFontSize(vg, 12.f);
        float bounds[4];
        const float textWidth = nvgTextBounds(vg, 0, 0, name.c_str(), nullptr, bounds);
        const float icon = 16.f, gap = 5.f;
        const float x = (box.size.x - icon - gap - textWidth) / 2;
        drawSvg(vg, *logo, x, 3.f, icon / 64.f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, x + icon + gap, 11.5f, name.c_str(), nullptr);
        const float markSize = 14.f;
        drawSvg(vg, *mark, 6.f, box.size.y - markSize - 3.f, markSize / 64.f);
    }
};

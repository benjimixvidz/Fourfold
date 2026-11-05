#pragma once
#include "ui.hpp"

namespace ui_tetrad {

// Transport widgets that TetradWidget places over the inspector (inspector coordinates).
const math::Rect kPlayRect{{2, 2}, {44, 30}};
const math::Rect kResetRect{{48, 2}, {44, 30}};
const Vec kClockLightPos{180, 9};

// Left half of Tetrad: transport, track settings, scale, generator and the selected steps. Every
// value is edited where it is shown: drag vertically (Ctrl: fine), double-click for the default,
// click a list or right-click for the values or a typed entry.
struct Inspector : OpaqueWidget {
    Tetrad* module = nullptr;
    UiState* ui = nullptr;

    enum class Drag { None, Field, Tempo, Pulses, Rotation };
    Drag dragKind = Drag::None;
    int dragField = -1;
    bool dragFine = false;
    float dragAccum = 0.f;
    float dragOld = 0.f;
    bool dragMoved = false;
    Gesture gesture;
    int hoverField = -1;
    int hoverButton = -1;
    Vec lastHover;

    Inspector();
    bool expert() const;   // every setting shown (EXPERT on)
    bool drum() const;     // the viewed track is in Drum mode
    int hitRow() const;    // Drum: the focused row, whose hits the step fields edit; else -1
    std::string rowText(int field) const;
    void nudgeRow(int field, int delta);
    void setRow(int field, int value);
    int paramId(int field) const;
    std::string stepText(int field) const;
    void nudgeField(int field, int delta);
    void resetField(int field);
    void fieldMenu(int field);
    void ppqnMenu();
    void pressButton(int button);

    void drawLayer(const DrawArgs& args, int layer) override;
    void onButton(const ButtonEvent& e) override;
    void onDragMove(const DragMoveEvent& e) override;
    void onDragEnd(const DragEndEvent& e) override;
    void onDoubleClick(const DoubleClickEvent& e) override;
    void onHover(const HoverEvent& e) override;
    void onLeave(const LeaveEvent& e) override;
};

} // namespace ui_tetrad

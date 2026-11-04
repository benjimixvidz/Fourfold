#pragma once
#include "ui.hpp"

namespace ui_tetrad {

// Right half of Tetrad: track and lane tabs, the 128-step overview with the loop handles, the
// 16x8 grid (click, paint, select) and the row labels.
struct GridPanel : OpaqueWidget {
    Tetrad* module = nullptr;
    UiState* ui = nullptr;

    enum class Mode { None, Paint, Select, LoopStart, LoopEnd, LoopMove, Scroll };
    Mode mode = Mode::None;
    Gesture gesture;
    Vec pressPos, dragPos;
    bool paintOn = true;
    int lastCol = -1, lastRow = -1;
    int dragStart = 0, dragLength = 16, grabOffset = 0;
    float scrollAccum = 0.f;
    int hoverCol = -1, hoverRow = -1, hoverLane = -1;

    GridPanel();
    bool poly() const;
    int playhead() const;
    bool playing() const;

    bool cellAt(Vec p, int& col, int& row) const;
    int overviewStep(float x) const;
    void paintCell(int col, int row, bool first);
    void selectTo(int col);
    void stepMenu(int col);
    void scroll(int delta);
    void setLoop(int start, int length, const std::string& name);

    void step() override;
    void drawLayer(const DrawArgs& args, int layer) override;
    void onButton(const ButtonEvent& e) override;
    void onDragMove(const DragMoveEvent& e) override;
    void onDragEnd(const DragEndEvent& e) override;
    void onDoubleClick(const DoubleClickEvent& e) override;
    void onHoverScroll(const HoverScrollEvent& e) override;
    void onHover(const HoverEvent& e) override;
    void onLeave(const LeaveEvent& e) override;
};

} // namespace ui_tetrad

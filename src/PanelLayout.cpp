/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "PanelLayout.h"

int ClampPanelWidth(int width, int minimum) {
    return limitValue(width, minimum, kPanelMaxWidth);
}

PanelWidths CalcPanelWidths(int available, int dpi, int tools, int comments) {
    auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
    PanelWidths result;
    int handle = scale(kPanelHandleWidth);
    int budget = std::max(0, available - scale(kDocumentMinWidth));
    int toolsMin = tools ? scale(kToolsMinWidth) : 0;
    int commentsMin = comments ? scale(kCommentsMinWidth) : 0;
    result.tools = tools ? scale(ClampPanelWidth(tools, kToolsMinWidth)) : 0;
    result.comments = comments ? scale(ClampPanelWidth(comments, kCommentsMinWidth)) : 0;
    int handles = (tools ? handle : 0) + (comments ? handle : 0);
    int excess = result.tools + result.comments + handles - budget;
    if (excess <= 0) return result;

    int toolsSlack = result.tools - toolsMin;
    int slack = toolsSlack + result.comments - commentsMin;
    int shrink = std::min(excess, slack);
    int toolsShrink = slack ? MulDiv(shrink, toolsSlack, slack) : 0;
    result.tools -= toolsShrink;
    result.comments -= shrink - toolsShrink;
    if (excess <= slack) return result;

    // Collapse in a stable order without changing the saved preferences.
    if (tools) {
        result.tools = scale(kToolsRailWidth);
        result.rail = true;
    }
    if (result.tools + result.comments + handles > budget) {
        result.comments = 0;
        handles = tools ? handle : 0;
    }
    if (result.tools + handles > budget) {
        result.tools = 0;
        result.rail = false;
    }
    return result;
}

#if IS_DEBUG
#include "base/tests/UtAssert.h"
void PanelLayout_UnitTests() {
    const int dpis[] = {96, 144, 192};
    for (int dpi : dpis) {
        auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
        auto full = CalcPanelWidths(scale(1200), dpi, 300, 360);
        utassert(full.tools == scale(300) && full.comments == scale(360) && !full.rail);
        auto shrink = CalcPanelWidths(scale(900), dpi, 300, 360);
        utassert(shrink.tools >= scale(180) && shrink.tools < full.tools);
        utassert(shrink.comments >= scale(220) && shrink.comments < full.comments);
        auto rail = CalcPanelWidths(scale(640), dpi, 300, 360);
        utassert(rail.rail && rail.tools == scale(52) && rail.comments == scale(220));
        auto hidden = CalcPanelWidths(scale(580), dpi, 300, 360);
        utassert(hidden.rail && hidden.comments == 0);
        for (int width = 0; width <= scale(1400); width++) {
            auto p = CalcPanelWidths(width, dpi, 300, 360);
            int used = p.tools + p.comments + (p.tools ? scale(5) : 0) + (p.comments ? scale(5) : 0);
            utassert(used <= std::max(0, width - scale(320)));
        }
        auto restored = CalcPanelWidths(scale(1200), dpi, 300, 360);
        utassert(restored.tools == full.tools && restored.comments == full.comments);
        auto off = CalcPanelWidths(scale(1200), dpi, 0, 0);
        utassert(off.tools == 0 && off.comments == 0);
    }
    utassert(ClampPanelWidth(-1, kToolsMinWidth) == kToolsMinWidth);
    utassert(ClampPanelWidth(900, kCommentsMinWidth) == kPanelMaxWidth);
}
#endif

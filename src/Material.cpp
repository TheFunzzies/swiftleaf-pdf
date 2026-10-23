/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"

#include "Theme.h"
#include "Material.h"

// Neutral panels and a blue accent keep tools separate from document content.
static const M3Scheme kLight = {
    .primary = MkRgb(0x1b, 0x63, 0xe0),
    .onPrimary = MkRgb(0xff, 0xff, 0xff),
    .primaryContainer = MkRgb(0xe3, 0xec, 0xfd),
    .onPrimaryContainer = MkRgb(0x17, 0x55, 0xc4),
    .secondaryContainer = MkRgb(0xe6, 0xee, 0xfd),
    .onSecondaryContainer = MkRgb(0x17, 0x55, 0xc4),
    .surface = MkRgb(0xf1, 0xf1, 0xf3),
    .onSurface = MkRgb(0x22, 0x22, 0x26),
    .onSurfaceVariant = MkRgb(0x44, 0x44, 0x4b),
    .surfaceContainerLowest = MkRgb(0xff, 0xff, 0xff),
    .surfaceContainerLow = MkRgb(0xff, 0xff, 0xff),
    .surfaceContainer = MkRgb(0xe9, 0xe9, 0xeb),
    .surfaceContainerHigh = MkRgb(0xf0, 0xf0, 0xf3),
    .surfaceContainerHighest = MkRgb(0xe4, 0xe4, 0xe9),
    .outline = MkRgb(0xda, 0xda, 0xde),
    .outlineVariant = MkRgb(0xe8, 0xe8, 0xeb),
};

static const M3Scheme kDark = {
    .primary = MkRgb(0x4b, 0x8b, 0xf5),
    .onPrimary = MkRgb(0xff, 0xff, 0xff),
    .primaryContainer = MkRgb(0x24, 0x36, 0x57),
    .onPrimaryContainer = MkRgb(0x8c, 0xb6, 0xfa),
    .secondaryContainer = MkRgb(0x23, 0x3a, 0x63),
    .onSecondaryContainer = MkRgb(0x8c, 0xb6, 0xfa),
    .surface = MkRgb(0x19, 0x19, 0x1c),
    .onSurface = MkRgb(0xec, 0xec, 0xef),
    .onSurfaceVariant = MkRgb(0xd4, 0xd4, 0xda),
    .surfaceContainerLowest = MkRgb(0x1e, 0x1e, 0x22),
    .surfaceContainerLow = MkRgb(0x26, 0x26, 0x2a),
    .surfaceContainer = MkRgb(0x1b, 0x1b, 0x1e),
    .surfaceContainerHigh = MkRgb(0x34, 0x34, 0x3a),
    .surfaceContainerHighest = MkRgb(0x3e, 0x3e, 0x45),
    .outline = MkRgb(0x3c, 0x3c, 0x43),
    .outlineVariant = MkRgb(0x33, 0x33, 0x39),
};

bool M3IsDark() {
    return !IsLightColor(ThemeControlBackgroundColor());
}

const M3Scheme& M3() {
    return M3IsDark() ? kDark : kLight;
}

Color M3StateLayer(Color container, Color content, int opacityPct) {
    u8 r1, g1, b1, r2, g2, b2;
    UnpackColor(container, r1, g1, b1);
    UnpackColor(content, r2, g2, b2);
    auto mix = [opacityPct](u8 bg, u8 fg) { return (u8)((fg * opacityPct + bg * (100 - opacityPct)) / 100); };
    return MkRgb(mix(r1, r2), mix(g1, g2), mix(b1, b2));
}

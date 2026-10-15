/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"

#include "Theme.h"
#include "Material.h"

// Neutral document chrome with Swiftleaf's green reserved for active controls.
static const M3Scheme kLight = {
    .primary = MkRgb(0x00, 0x6b, 0x5a),
    .onPrimary = MkRgb(0xff, 0xff, 0xff),
    .primaryContainer = MkRgb(0x9f, 0xf2, 0xdb),
    .onPrimaryContainer = MkRgb(0x00, 0x20, 0x1a),
    .secondaryContainer = MkRgb(0xcd, 0xe8, 0xe0),
    .onSecondaryContainer = MkRgb(0x06, 0x20, 0x1b),
    .surface = MkRgb(0xf5, 0xf6, 0xf7),
    .onSurface = MkRgb(0x17, 0x1d, 0x1b),
    .onSurfaceVariant = MkRgb(0x3f, 0x49, 0x45),
    .surfaceContainerLowest = MkRgb(0xff, 0xff, 0xff),
    .surfaceContainerLow = MkRgb(0xf5, 0xf6, 0xf7),
    .surfaceContainer = MkRgb(0xee, 0xf0, 0xf2),
    .surfaceContainerHigh = MkRgb(0xe6, 0xe8, 0xeb),
    .surfaceContainerHighest = MkRgb(0xdc, 0xdf, 0xe3),
    .outline = MkRgb(0x6f, 0x79, 0x75),
    .outlineVariant = MkRgb(0xd5, 0xda, 0xdf),
};

static const M3Scheme kDark = {
    .primary = MkRgb(0x83, 0xd5, 0xc0),
    .onPrimary = MkRgb(0x00, 0x38, 0x2e),
    .primaryContainer = MkRgb(0x00, 0x51, 0x44),
    .onPrimaryContainer = MkRgb(0x9f, 0xf2, 0xdb),
    .secondaryContainer = MkRgb(0x33, 0x4b, 0x45),
    .onSecondaryContainer = MkRgb(0xcd, 0xe8, 0xe0),
    .surface = MkRgb(0x18, 0x1a, 0x1d),
    .onSurface = MkRgb(0xdd, 0xe4, 0xe1),
    .onSurfaceVariant = MkRgb(0xbf, 0xc9, 0xc4),
    .surfaceContainerLowest = MkRgb(0x13, 0x15, 0x18),
    .surfaceContainerLow = MkRgb(0x20, 0x23, 0x27),
    .surfaceContainer = MkRgb(0x27, 0x2b, 0x30),
    .surfaceContainerHigh = MkRgb(0x30, 0x35, 0x3b),
    .surfaceContainerHighest = MkRgb(0x3a, 0x40, 0x47),
    .outline = MkRgb(0x89, 0x93, 0x8f),
    .outlineVariant = MkRgb(0x3e, 0x44, 0x4c),
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

/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"

#include "Theme.h"
#include "Material.h"

// generated from seed #0E7C66 with the Material Theme Builder tonal palettes
static const M3Scheme kLight = {
    .primary = MkRgb(0x00, 0x6b, 0x5a),
    .onPrimary = MkRgb(0xff, 0xff, 0xff),
    .primaryContainer = MkRgb(0x9f, 0xf2, 0xdb),
    .onPrimaryContainer = MkRgb(0x00, 0x20, 0x1a),
    .secondaryContainer = MkRgb(0xcd, 0xe8, 0xe0),
    .onSecondaryContainer = MkRgb(0x06, 0x20, 0x1b),
    .surface = MkRgb(0xf4, 0xfb, 0xf8),
    .onSurface = MkRgb(0x17, 0x1d, 0x1b),
    .onSurfaceVariant = MkRgb(0x3f, 0x49, 0x45),
    .surfaceContainerLowest = MkRgb(0xff, 0xff, 0xff),
    .surfaceContainerLow = MkRgb(0xef, 0xf5, 0xf2),
    .surfaceContainer = MkRgb(0xe9, 0xef, 0xec),
    .surfaceContainerHigh = MkRgb(0xe3, 0xea, 0xe7),
    .surfaceContainerHighest = MkRgb(0xdd, 0xe4, 0xe1),
    .outline = MkRgb(0x6f, 0x79, 0x75),
    .outlineVariant = MkRgb(0xbf, 0xc9, 0xc4),
};

static const M3Scheme kDark = {
    .primary = MkRgb(0x83, 0xd5, 0xc0),
    .onPrimary = MkRgb(0x00, 0x38, 0x2e),
    .primaryContainer = MkRgb(0x00, 0x51, 0x44),
    .onPrimaryContainer = MkRgb(0x9f, 0xf2, 0xdb),
    .secondaryContainer = MkRgb(0x33, 0x4b, 0x45),
    .onSecondaryContainer = MkRgb(0xcd, 0xe8, 0xe0),
    .surface = MkRgb(0x0e, 0x15, 0x13),
    .onSurface = MkRgb(0xdd, 0xe4, 0xe1),
    .onSurfaceVariant = MkRgb(0xbf, 0xc9, 0xc4),
    .surfaceContainerLowest = MkRgb(0x09, 0x0f, 0x0e),
    .surfaceContainerLow = MkRgb(0x17, 0x1d, 0x1b),
    .surfaceContainer = MkRgb(0x1b, 0x21, 0x1f),
    .surfaceContainerHigh = MkRgb(0x25, 0x2b, 0x29),
    .surfaceContainerHighest = MkRgb(0x30, 0x36, 0x34),
    .outline = MkRgb(0x89, 0x93, 0x8f),
    .outlineVariant = MkRgb(0x3f, 0x49, 0x45),
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

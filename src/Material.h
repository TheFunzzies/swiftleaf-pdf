/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// Material Design 3 color roles for Swiftleaf's chrome (ribbon, navigation
// rail, status bar), from the teal seed color. The dark scheme is used when the
// app's theme is dark. https://m3.material.io/styles/color/roles

struct M3Scheme {
    Color primary;
    Color onPrimary;
    Color primaryContainer;
    Color onPrimaryContainer;
    Color secondaryContainer;
    Color onSecondaryContainer;
    Color surface;
    Color onSurface;
    Color onSurfaceVariant;
    Color surfaceContainerLowest;
    Color surfaceContainerLow;
    Color surfaceContainer;
    Color surfaceContainerHigh;
    Color surfaceContainerHighest;
    Color outline;
    Color outlineVariant;
};

const M3Scheme& M3();
bool M3IsDark();

// Material's state layers: the content color over a container at a set opacity
constexpr int kM3HoverOpacity = 8;
constexpr int kM3PressedOpacity = 12;
Color M3StateLayer(Color container, Color content, int opacityPct);

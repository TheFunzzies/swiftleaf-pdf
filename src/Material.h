/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// Color roles for Swiftleaf's ribbon, navigation rail and status bar.

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
constexpr int kChromeCornerDiameter = 12;
Color M3StateLayer(Color container, Color content, int opacityPct);

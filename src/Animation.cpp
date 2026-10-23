/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "Animation.h"

static Vec<Animation*> gAnimations;
static Vec<HWND> gResizeWindows;
static UINT_PTR gAnimationTimer = 0;
constexpr int kAnimationMs = 160;

int AnimationDuration(bool clientAreaAnimation, bool resizing) {
    return clientAreaAnimation && !resizing ? kAnimationMs : 0;
}

float AnimationEase(float progress) {
    float left = 1 - limitValue(progress, 0.f, 1.f);
    return 1 - left * left * left;
}

bool AnimationAllowed(HWND hwnd) {
    BOOL enabled = FALSE;
    bool ok = SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) != FALSE;
    HWND frame = GetAncestor(hwnd, GA_ROOT);
    return hwnd && IsWindowVisible(hwnd) && AnimationDuration(ok && enabled, VecFind(gResizeWindows, frame) >= 0) != 0;
}

static void StopIdleTimer() {
    if (len(gAnimations) || !gAnimationTimer) return;
    KillTimer(nullptr, gAnimationTimer);
    gAnimationTimer = 0;
}

Animation::~Animation() {
    VecRemove(gAnimations, this);
    StopIdleTimer();
}

void Animation::Snap(float next) {
    VecRemove(gAnimations, this);
    value = from = target = next;
    initialized = true;
    running = false;
    StopIdleTimer();
}

static void CALLBACK AnimationTick(HWND, UINT, UINT_PTR, DWORD) {
    ULONGLONG now = GetTickCount64();
    for (int i = len(gAnimations) - 1; i >= 0; i--) {
        auto* a = gAnimations[i];
        float elapsed = (float)(now - a->start) / kAnimationMs;
        bool finish = elapsed >= 1 || !AnimationAllowed(a->hwnd);
        a->value = finish ? a->target : a->from + (a->target - a->from) * AnimationEase(elapsed);
        HWND hwnd = a->hwnd;
        auto onTick = a->onTick;
        void* context = a->context;
        if (finish) a->Snap(a->target);
        InvalidateRect(hwnd, nullptr, FALSE);
        if (onTick) onTick(context);
    }
    StopIdleTimer();
}

float Animation::To(HWND window, float next) {
    hwnd = window;
    if (!initialized || !AnimationAllowed(hwnd)) {
        Snap(next);
        return value;
    }
    if (next == target) return value;
    from = value;
    target = next;
    start = GetTickCount64();
    if (!running) VecAppend(gAnimations, this);
    running = true;
    if (!gAnimationTimer) gAnimationTimer = SetTimer(nullptr, 0, 16, AnimationTick);
    if (!gAnimationTimer) Snap(next);
    return value;
}

void AnimationSuspend(HWND hwnd, bool suspend) {
    HWND frame = GetAncestor(hwnd, GA_ROOT);
    if (suspend && VecFind(gResizeWindows, frame) < 0) VecAppend(gResizeWindows, frame);
    if (!suspend) VecRemove(gResizeWindows, frame);
    if (!suspend) return;
    for (int i = len(gAnimations) - 1; i >= 0; i--) {
        auto* a = gAnimations[i];
        if (GetAncestor(a->hwnd, GA_ROOT) != frame) continue;
        HWND window = a->hwnd;
        auto onTick = a->onTick;
        void* context = a->context;
        a->Snap(a->target);
        InvalidateRect(window, nullptr, FALSE);
        if (onTick) onTick(context);
    }
}

void AnimationForget(HWND hwnd) {
    AnimationSuspend(hwnd, true);
    AnimationSuspend(hwnd, false);
}

int AnimationCount(HWND hwnd) {
    int count = 0;
    HWND frame = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
    for (auto* a : gAnimations) {
        if (!hwnd || GetAncestor(a->hwnd, GA_ROOT) == frame) count++;
    }
    return count;
}

Color AnimationBlend(Color from, Color to, float progress) {
    float p = limitValue(progress, 0.f, 1.f);
    auto blend = [p](u8 a, u8 b) { return (u8)(a + (b - a) * p + .5f); };
    return MkRgba(blend(GetRed(from), GetRed(to)), blend(GetGreen(from), GetGreen(to)),
                  blend(GetBlue(from), GetBlue(to)), blend(GetAlpha(from), GetAlpha(to)));
}

Color AnimatedColor::Get(HWND hwnd, Color next) {
    if (target == kColorUnset) {
        from = target = next;
        progress.Snap(1);
    } else if (target != next) {
        from = AnimationBlend(from, target, progress.value);
        target = next;
        progress.Snap(0);
    }
    return AnimationBlend(from, target, progress.To(hwnd, 1));
}

#if IS_DEBUG
#include "base/tests/UtAssert.h"
void Animation_UnitTests() {
    utassert(AnimationDuration(true, false) >= 120 && AnimationDuration(true, false) <= 200);
    utassert(AnimationDuration(false, false) == 0);
    utassert(AnimationDuration(true, true) == 0);
    utassert(AnimationDuration(false, true) == 0);
    utassert(AnimationEase(0) == 0 && AnimationEase(1) == 1);
    utassert(AnimationEase(.5f) > .5f);
    utassert(AnimationEase(-1) == 0 && AnimationEase(2) == 1);
    Animation a;
    a.Snap(20);
    utassert(a.To(nullptr, 40) == 40 && !a.running);
    utassert(gAnimationTimer == 0 && len(gAnimations) == 0);
    utassert(AnimationBlend(MkRgb(0, 0, 0), MkRgb(200, 100, 60), .5f) == MkRgb(100, 50, 30));
}
#endif

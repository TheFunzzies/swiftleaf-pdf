/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

int AnimationDuration(bool clientAreaAnimation, bool resizing);
float AnimationEase(float progress);
bool AnimationAllowed(HWND);
void AnimationSuspend(HWND, bool suspend);
void AnimationForget(HWND);

struct Animation {
    float value = 0;
    float from = 0;
    float target = 0;
    ULONGLONG start = 0;
    HWND hwnd = nullptr;
    void (*onTick)(void*) = nullptr;
    void* context = nullptr;
    bool initialized = false;
    bool running = false;

    Animation() = default;
    Animation(const Animation&) = delete;
    Animation& operator=(const Animation&) = delete;
    ~Animation();
    void Snap(float);
    float To(HWND, float);
};

Color AnimationBlend(Color from, Color to, float progress);
struct AnimatedColor {
    Animation progress;
    Color from = kColorUnset;
    Color target = kColorUnset;
    Color Get(HWND, Color);
};

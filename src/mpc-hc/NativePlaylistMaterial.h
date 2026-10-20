#pragma once
#include <memory>

// Optional Windows App SDK controller; the public header has no WinRT dependency.
class CNativePlaylistMaterial {
    struct State;
    std::unique_ptr<State> m_state;
public:
    static constexpr unsigned SlideDurationMs = 300;
    static constexpr unsigned FadeDurationMs = 160;
    CNativePlaylistMaterial();
    ~CNativePlaylistMaterial();
    bool Start(HWND window, int mode);
    void Reset();
    bool Active() const;
    bool UpdateForeground(const void* premultipliedBGRA, int width, int height);
    bool Ready() const;
    bool PrepareSlide(float radius);
    bool StartSlide();
    bool StartFade();
    void FinishSlide();
    void SetVisible(bool visible);
};

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
    // Grouped panels animate the brush inside Root. A header instead moves
    // its HWND with the chrome host and keeps the asynchronous system slot.
    bool Start(HWND window, int mode, bool groupedBackdrop = true);
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

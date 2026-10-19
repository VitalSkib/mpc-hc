#pragma once
#include <memory>

// Optional Windows App SDK controller; the public header has no WinRT dependency.
class CNativePlaylistMaterial {
    struct State;
    std::unique_ptr<State> m_state;
public:
    CNativePlaylistMaterial();
    ~CNativePlaylistMaterial();
    bool Start(HWND window, int mode);
    void Reset();
    bool Active() const;
    bool UpdateForeground(const void* premultipliedBGRA, int width, int height);
};

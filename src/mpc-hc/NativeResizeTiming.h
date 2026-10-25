#pragma once

// Opt-in UI-thread measurements. No file I/O, strings or HWND queries occur
// inside the sizing loop; only QPC and bounded writes to a fixed buffer.
#include <array>
#include <string>

namespace NativeResizeTiming {
enum Stage { FrameSize, FrameLayout, MoveVideo, Decoration, Hosts, Toolbar,
    Footer, Header, StageCount };
inline constexpr const char* names[] = { "frame.size", "frame.layout", "video.move",
    "chrome.update", "chrome.hosts", "toolbar.prepare", "footer.refresh", "header.refresh" };
struct Event { LONGLONG start=0, elapsed=0; int stage=0, depth=0, width=-1, height=-1; };
struct Total { LONGLONG ticks=0, maximum=0; unsigned count=0; };
struct State {
    bool active=false;
    LARGE_INTEGER frequency{}, start{};
    std::array<Event,4096> events{};
    std::array<Total,StageCount> totals{};
    size_t used=0;
    unsigned dropped=0;
    int depth=0;
};
inline State state;
inline void Begin() {
    if (state.active || !AfxGetApp()->GetProfileInt(L"Settings", L"FluentResizeTiming", 0)) return;
    state = State{};
    if (!QueryPerformanceFrequency(&state.frequency)) return;
    QueryPerformanceCounter(&state.start);
    state.active=true;
}
class Scope {
    bool m_active=false;
    Event m_event;
public:
    explicit Scope(Stage stage) : m_active(state.active) {
        if (m_active) {
            LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
            m_event.start=now.QuadPart;
            m_event.stage=stage; m_event.depth=state.depth++;
        }
    }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
    void Bounds(int width, int height) { m_event.width=width; m_event.height=height; }
    ~Scope() {
        if (!m_active) return;
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        m_event.elapsed=now.QuadPart-m_event.start;
        --state.depth;
        auto& total=state.totals[m_event.stage];
        ++total.count; total.ticks+=m_event.elapsed;
        if (m_event.elapsed>total.maximum) total.maximum=m_event.elapsed;
        if (state.used<state.events.size()) state.events[state.used++]=m_event;
        else ++state.dropped;
    }
};
inline void End() {
    if (!state.active) return;
    state.active=false;
    // Formatting and writing happen once, after the native drag has ended.
    wchar_t path[32768]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,_countof(path));
    if (!length || length>=_countof(path)) return;
    wchar_t* leaf=wcsrchr(path,L'\\');
    if (!leaf) return;
    wcscpy_s(leaf+1,_countof(path)-(leaf+1-path),L"MPC-HC-resize-profile.csv");
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) return;
    std::string text="# Resize timing v1; UI thread; inclusive nested durations; no GPU/DWM timestamps\r\n";
    char line[256]{};
    sprintf_s(line,"# events=%zu,dropped=%u\r\n",state.used,state.dropped); text+=line;
    text+="# stage,count,total_ms,max_ms\r\n";
    const double scale=1000.0/state.frequency.QuadPart;
    for (int i=0;i<StageCount;++i) {
        const auto& total=state.totals[i];
        sprintf_s(line,"# %s,%u,%.4f,%.4f\r\n",names[i],total.count,total.ticks*scale,total.maximum*scale);
        text+=line;
    }
    text+="stage,start_ms,duration_ms,depth,width,height\r\n";
    for (size_t i=0;i<state.used;++i) {
        const auto& event=state.events[i];
        sprintf_s(line,"%s,%.4f,%.4f,%d,%d,%d\r\n",names[event.stage],
            (event.start-state.start.QuadPart)*scale,event.elapsed*scale,event.depth,event.width,event.height);
        text+=line;
    }
    DWORD written=0; WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr);
    CloseHandle(file);
}
}

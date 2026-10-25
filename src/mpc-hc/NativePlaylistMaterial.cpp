#define NOMINMAX
#include <windows.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdint>
#include <chrono>
#include "NativePlaylistMaterial.h"
#include "NativePlaylistDiagnostic.h"

#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
#include <appmodel.h>
#include <roapi.h>
#include <dwmapi.h>
#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include "wasdk-probe/bootstrap-bytes.h"
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "CoreMessaging.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")

namespace {
void MaterialLog(const char* stage, HRESULT result)
{
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"MPC-HC-material.log");
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[180];
    const int length = sprintf_s(line, "%s: 0x%08lX\r\n", stage, static_cast<unsigned long>(result));
    DWORD written;
    if (length > 0) WriteFile(file, line, length, &written, nullptr);
    CloseHandle(file);
}

HRESULT LoadRuntime()
{
    // Bootstrap only adds the already installed runtime to this process. No installer,
    // registration, or changes to the player's directory are necessary.
    static const HRESULT result = []() -> HRESULT {
        wchar_t temp[MAX_PATH] = {};
        if (!GetTempPathW(MAX_PATH, temp)) return HRESULT_FROM_WIN32(GetLastError());
        const std::wstring directory = std::wstring(temp) + L"MPC-HC-Fluent-WASDK-1.8.250906002";
        if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return HRESULT_FROM_WIN32(GetLastError());
        const std::wstring path = directory + L"\\Microsoft.WindowsAppRuntime.Bootstrap.dll";
        bool valid = false;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            if (GetFileSize(file, nullptr) == sizeof(kPlaylistBootstrap)) {
                std::vector<BYTE> bytes(sizeof(kPlaylistBootstrap)); DWORD read = 0;
                valid = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)
                    && read == bytes.size() && !memcmp(bytes.data(), kPlaylistBootstrap, bytes.size());
            }
            CloseHandle(file);
        }
        if (!valid) {
            file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
            DWORD written = 0;
            const BOOL ok = WriteFile(file, kPlaylistBootstrap, static_cast<DWORD>(sizeof(kPlaylistBootstrap)), &written, nullptr);
            const DWORD error = GetLastError(); CloseHandle(file);
            if (!ok || written != sizeof(kPlaylistBootstrap)) return HRESULT_FROM_WIN32(ok ? ERROR_WRITE_FAULT : error);
        }
        // Retain the runtime and module for process lifetime: cached WinRT factories
        // must not outlive their package graph. Windows releases them on process exit.
        const HMODULE bootstrap = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!bootstrap) return HRESULT_FROM_WIN32(GetLastError());
        using Initialize = HRESULT(WINAPI*)(UINT32, PCWSTR, PACKAGE_VERSION);
        const auto initialize = reinterpret_cast<Initialize>(GetProcAddress(bootstrap, "MddBootstrapInitialize"));
        if (!initialize) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        return initialize(0x00010008, L"", PACKAGE_VERSION{});
    }();
    return result;
}
}

struct CNativePlaylistMaterial::State {
    winrt::Windows::UI::Composition::Compositor compositor{nullptr};
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target{nullptr};
    winrt::Windows::UI::Composition::CompositionGraphicsDevice graphics{nullptr};
    winrt::Windows::UI::Composition::CompositionDrawingSurface foreground{nullptr};
    winrt::Windows::UI::Composition::SpriteVisual artwork{nullptr};
    winrt::Windows::UI::Composition::ContainerVisual viewport{nullptr}, panel{nullptr}, content{nullptr};
    winrt::Windows::UI::Composition::SpriteVisual shadowVisual{nullptr}, material{nullptr};
    winrt::Windows::UI::Composition::CompositionDrawingSurface shadowMask{nullptr};
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry foregroundShape{nullptr};
    int width = 0, height = 0;
    float radius = 8.f;
    winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropConfiguration configuration{nullptr};
    winrt::Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicController acrylic{nullptr};
    winrt::Microsoft::UI::Composition::SystemBackdrops::MicaController mica{nullptr};
    bool apartment = false;
    bool groupedBackdrop = true;
    int lastState = -1;
    bool SynchronizeBrush() {
        auto backdrop = target.as<winrt::Windows::UI::Composition::ICompositionSupportsSystemBackdrop>();
        if (!groupedBackdrop) return backdrop.SystemBackdrop() != nullptr;
        if (auto brush = backdrop.SystemBackdrop()) {
            material.Brush(brush);
            backdrop.SystemBackdrop(nullptr);
            MaterialLog("System material brush routed into clipped panel", S_OK);
        }
        return material && material.Brush();
    }
    ~State() {
        try { if (acrylic) acrylic.Close(); if (mica) mica.Close(); } catch (...) {}
        acrylic = nullptr; mica = nullptr; configuration = nullptr;
        try { if (target) { target.Root(nullptr); target.Close(); } } catch (...) {}
        artwork = nullptr; foreground = nullptr; foregroundShape = nullptr; shadowMask = nullptr;
        material = nullptr; shadowVisual = nullptr; content = nullptr; panel = nullptr; viewport = nullptr;
        graphics = nullptr; target = nullptr;
        try { if (compositor) compositor.Close(); } catch (...) {}
        compositor = nullptr;
        if (apartment) RoUninitialize();
    }
};

bool CNativePlaylistMaterial::Start(HWND window, int mode, bool groupedBackdrop)
{
    Reset();
    if (mode != 2 && mode != 3) return false;
    auto state = std::make_unique<State>();
    state->groupedBackdrop = groupedBackdrop;
    if (!groupedBackdrop) state->radius = 0.f;
    const HRESULT apartment = RoInitialize(RO_INIT_SINGLETHREADED);
    MaterialLog("RoInitialize STA", apartment);
    if (FAILED(apartment)) return false;
    state->apartment = true;
    const HRESULT runtime = LoadRuntime(); MaterialLog("Bootstrap 1.8", runtime);
    if (FAILED(runtime)) return false;
    try {
        // Retain our UI dispatcher for process lifetime so changing material
        // does not shut down a queue that other composition objects still use.
        static winrt::Windows::System::DispatcherQueueController queue{nullptr};
        if (!winrt::Windows::System::DispatcherQueue::GetForCurrentThread()) {
            DispatcherQueueOptions options{sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE};
            winrt::check_hresult(CreateDispatcherQueueController(options,
                reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(queue))));
        }
        MaterialLog("DispatcherQueue", S_OK);
        const BOOL enable = TRUE;
        // DWM attributes reject child HWNDs. Header composition is hosted in
        // a child, while activation/policy belong to the real root window.
        const HWND policyWindow = groupedBackdrop ? window : GetAncestor(window, GA_ROOT);
        winrt::check_hresult(DwmSetWindowAttribute(policyWindow, static_cast<DWMWINDOWATTRIBUTE>(17), &enable, sizeof(enable)));
        state->compositor = winrt::Windows::UI::Composition::Compositor();
        auto interop = state->compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
        winrt::check_hresult(interop->CreateDesktopWindowTarget(window, true,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(state->target))));
        state->target.Root(state->compositor.CreateContainerVisual());
        MaterialLog("Composition target and root", S_OK);
        namespace sb = winrt::Microsoft::UI::Composition::SystemBackdrops;
        state->configuration = sb::SystemBackdropConfiguration();
        state->configuration.Theme(sb::SystemBackdropTheme::Dark);
        // Diagnostic policy: isolate the material from popup and color-picker focus.
        state->configuration.IsInputActive(true);
        const winrt::Microsoft::UI::WindowId id{reinterpret_cast<uint64_t>(policyWindow)};
        bool attached = false;
        if (mode == 3) {
            if (!sb::DesktopAcrylicController::IsSupported()) { MaterialLog("Acrylic unsupported", E_NOTIMPL); return false; }
            state->acrylic = sb::DesktopAcrylicController();
            state->acrylic.SetSystemBackdropConfiguration(state->configuration);
            state->acrylic.FallbackColor({255, 43, 43, 43});
            attached = state->acrylic.SetTarget(id, state->target);
            MaterialLog("Acrylic controller state", static_cast<HRESULT>(state->acrylic.State()));
        } else {
            if (!sb::MicaController::IsSupported()) { MaterialLog("Mica unsupported", E_NOTIMPL); return false; }
            state->mica = sb::MicaController();
            state->mica.SetSystemBackdropConfiguration(state->configuration);
            state->mica.FallbackColor({255, 43, 43, 43});
            attached = state->mica.SetTarget(id, state->target);
            MaterialLog("Mica controller state", static_cast<HRESULT>(state->mica.State()));
        }
        MaterialLog("SetTarget; input active = true", attached ? S_OK : E_FAIL);
        if (!attached) return false;
        // The legacy controller writes its brush to the target's system
        // backdrop slot, outside Root. Route that public brush into our own
        // SpriteVisual so it follows the same transform and clip as artwork.
        state->viewport = state->target.Root().as<winrt::Windows::UI::Composition::ContainerVisual>();
        // A hidden popup keeps an empty composition tree ready for the next
        // reveal, rather than retaining a fully visible cached first frame.
        state->viewport.IsVisible(false);
        state->panel = state->compositor.CreateContainerVisual();
        state->content = state->compositor.CreateContainerVisual();
        state->material = state->compositor.CreateSpriteVisual();
        state->content.Children().InsertAtBottom(state->material);
        state->panel.Children().InsertAtTop(state->content);
        state->viewport.Children().InsertAtTop(state->panel);
        state->viewport.Clip(state->compositor.CreateInsetClip());
        MaterialLog("Material subtree in fixed composition viewport", S_OK);
        // The controller's visual is above HWND GDI. Keep the complete native
        // artwork in the same visual tree, explicitly above the backdrop.
        winrt::com_ptr<ID3D11Device> d3d;
        HRESULT device = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            d3d.put(), nullptr, nullptr);
        if (FAILED(device)) winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            d3d.put(), nullptr, nullptr));
        winrt::com_ptr<ID2D1Factory1> factory;
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1), nullptr, factory.put_void()));
        winrt::com_ptr<ID2D1Device> d2d;
        winrt::check_hresult(factory->CreateDevice(d3d.as<IDXGIDevice>().get(), d2d.put()));
        winrt::check_hresult(state->compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>()->CreateGraphicsDevice(
            d2d.get(), reinterpret_cast<ABI::Windows::UI::Composition::ICompositionGraphicsDevice**>(winrt::put_abi(state->graphics))));
        state->artwork = state->compositor.CreateSpriteVisual();
        state->content.Children().InsertAtTop(state->artwork);
        state->shadowVisual = state->compositor.CreateSpriteVisual();
        auto shadow = state->compositor.CreateDropShadow();
        shadow.Color({255, 0, 0, 0}); shadow.Opacity(.3f);
        shadow.BlurRadius(12.f); shadow.Offset({0.f, 3.f, 0.f});
        state->shadowVisual.Shadow(shadow);
        state->panel.Children().InsertAtBottom(state->shadowVisual);
        MaterialLog("Foreground visual above material", S_OK);
        m_state = std::move(state);
        return true;
    } catch (const winrt::hresult_error& error) {
        MaterialLog("Controller setup failed", error.code());
    } catch (...) { MaterialLog("Controller setup failed", E_FAIL); }
    return false;
}
bool CNativePlaylistMaterial::UpdateForeground(const void* pixels, int width, int height)
{
    if (!m_state || !pixels || width <= 0 || height <= 0) return false;
    try {
        auto& state = *m_state;
        state.SynchronizeBrush();
        if (!state.foreground || state.width != width || state.height != height) {
            if (!state.foreground) {
                namespace dx = winrt::Windows::Graphics::DirectX;
                state.foreground = state.graphics.CreateDrawingSurface(
                    {static_cast<float>(width), static_cast<float>(height)},
                    dx::DirectXPixelFormat::B8G8R8A8UIntNormalized, dx::DirectXAlphaMode::Premultiplied);
                state.artwork.Brush(state.compositor.CreateSurfaceBrush(state.foreground));
                state.foregroundShape = state.compositor.CreateRoundedRectangleGeometry();
                state.content.Clip(state.compositor.CreateGeometricClip(state.foregroundShape));
                MaterialLog("Foreground surface created", S_OK);
            } else {
                // Preserve the surface and its brush during live sizing. Replacing
                // the brush on each WM_SIZE publishes an empty surface before its
                // first drawing transaction has finished.
                const SIZE size{width, height};
                winrt::check_hresult(state.foreground.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>()->Resize(size));
            }
            state.artwork.Size({static_cast<float>(width), static_cast<float>(height)});
            state.viewport.Size({static_cast<float>(width), static_cast<float>(height)});
            state.panel.Size({static_cast<float>(width), static_cast<float>(height)});
            state.content.Size({static_cast<float>(width), static_cast<float>(height)});
            state.shadowVisual.Size({static_cast<float>(width), static_cast<float>(height)});
            state.material.Size({static_cast<float>(width), static_cast<float>(height)});
            state.foregroundShape.Size({static_cast<float>(width), static_cast<float>(height)});
            state.foregroundShape.CornerRadius({state.radius, state.radius});
            state.width = width; state.height = height;
        }
        auto surface = state.foreground.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
        winrt::com_ptr<ID2D1DeviceContext> dc;
        POINT offset{};
        winrt::check_hresult(surface->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &offset));
        // Always finish a begun composition update, including an allocation failure.
        const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        winrt::com_ptr<ID2D1Bitmap1> bitmap;
        const HRESULT upload = dc->CreateBitmap(D2D1::SizeU(width, height), pixels, width * 4, &properties, bitmap.put());
        if (SUCCEEDED(upload)) {
            dc->SetDpi(96, 96);
            dc->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            dc->DrawBitmap(bitmap.get(), D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)));
        }
        const HRESULT end = surface->EndDraw();
        winrt::check_hresult(upload); winrt::check_hresult(end);
        return true;
    } catch (const winrt::hresult_error& error) { MaterialLog("Foreground update failed", error.code()); }
    return false;
}

bool CNativePlaylistMaterial::Ready() const
{
    if (!m_state) return false;
    try { return m_state->SynchronizeBrush(); }
    catch (const winrt::hresult_error& error) { MaterialLog("Route system material brush failed", error.code()); }
    return false;
}

bool CNativePlaylistMaterial::PrepareSlide(float radius)
{
    if (!m_state || !m_state->foreground) return false;
    try {
        auto& s = *m_state;
        s.radius = radius;
        s.foregroundShape.Size({static_cast<float>(s.width), static_cast<float>(s.height)});
        s.foregroundShape.CornerRadius({radius, radius});
        // Shadow and content share the moving parent. The explicit stationary
        // viewport clips both, including the blur outside the panel's bounds.
        namespace dx = winrt::Windows::Graphics::DirectX;
        s.shadowMask = s.graphics.CreateDrawingSurface(
            {static_cast<float>(s.width), static_cast<float>(s.height)},
            dx::DirectXPixelFormat::B8G8R8A8UIntNormalized, dx::DirectXAlphaMode::Premultiplied);
        auto surface = s.shadowMask.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
        winrt::com_ptr<ID2D1DeviceContext> dc; POINT offset{};
        winrt::check_hresult(surface->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &offset));
        winrt::com_ptr<ID2D1SolidColorBrush> white;
        const HRESULT brushResult = dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), white.put());
        if (SUCCEEDED(brushResult)) {
            dc->SetDpi(96, 96);
            dc->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.f, 0.f,
                static_cast<float>(s.width), static_cast<float>(s.height)), radius, radius), white.get());
        }
        const HRESULT end = surface->EndDraw();
        winrt::check_hresult(brushResult); winrt::check_hresult(end);
        s.shadowVisual.Shadow().as<winrt::Windows::UI::Composition::DropShadow>().Mask(
            s.compositor.CreateSurfaceBrush(s.shadowMask));
        s.panel.StopAnimation(L"Offset.Y");
        s.panel.Offset({0.f, static_cast<float>(s.height), 0.f});
        return true;
    } catch (const winrt::hresult_error& error) { MaterialLog("Prepare composition slide failed", error.code()); }
    return false;
}

bool CNativePlaylistMaterial::StartSlide()
{
    if (!m_state) return false;
    try {
        auto& s = *m_state;
        auto animation = s.compositor.CreateScalarKeyFrameAnimation();
        animation.Duration(std::chrono::milliseconds(SlideDurationMs));
        animation.InsertKeyFrame(0.f, static_cast<float>(s.height));
        animation.InsertKeyFrame(1.f, 0.f, s.compositor.CreateCubicBezierEasingFunction(
            {1.f / 3.f, 1.f}, {2.f / 3.f, 1.f}));
        s.panel.Offset({0.f, 0.f, 0.f});
        s.panel.StartAnimation(L"Offset.Y", animation);
        s.viewport.IsVisible(true);
        return true;
    } catch (const winrt::hresult_error& error) { MaterialLog("Start composition slide failed", error.code()); }
    return false;
}

bool CNativePlaylistMaterial::StartFade()
{
    if (!m_state) return false;
    try {
        auto& s = *m_state;
        if (PlaylistDiagnostic::Current().enabled) {
            auto backdrop = s.target.as<winrt::Windows::UI::Composition::ICompositionSupportsSystemBackdrop>();
            const ULONG_PTR flags = (backdrop.SystemBackdrop() ? 1 : 0)
                | (s.material.Brush() ? 2 : 0) | (s.panel.IsVisible() ? 4 : 0);
            PlaylistDiagnostic::Trace("composition-fade-backdrop-material-visible", nullptr, nullptr, 0, flags);
        }
        auto animation = s.compositor.CreateScalarKeyFrameAnimation();
        animation.Duration(std::chrono::milliseconds(FadeDurationMs));
        animation.InsertKeyFrame(0.f, 1.f);
        animation.InsertKeyFrame(1.f, 0.f, s.compositor.CreateLinearEasingFunction());
        s.panel.Opacity(0.f);
        s.panel.StartAnimation(L"Opacity", animation);
        return true;
    } catch (const winrt::hresult_error& error) { MaterialLog("Start composition fade failed", error.code()); }
    return false;
}

void CNativePlaylistMaterial::FinishSlide()
{
    if (!m_state) return;
    try {
        m_state->panel.StopAnimation(L"Offset.Y");
        m_state->panel.Offset({0.f, 0.f, 0.f});
    } catch (...) {}
}

void CNativePlaylistMaterial::SetVisible(bool visible)
{
    if (!m_state) return;
    try {
        m_state->viewport.IsVisible(visible);
        if (!visible) {
            m_state->panel.StopAnimation(L"Opacity");
            m_state->panel.Opacity(1.f);
            m_state->panel.StopAnimation(L"Offset.Y");
            m_state->panel.Offset({0.f, static_cast<float>(m_state->height), 0.f});
        } else m_state->panel.Offset({0.f, 0.f, 0.f});
    } catch (const winrt::hresult_error& error) { MaterialLog("Set composition visibility failed", error.code()); }
}

bool CNativePlaylistMaterial::Active() const
{
    if (!m_state) return false;
    try {
        m_state->SynchronizeBrush();
        namespace sb = winrt::Microsoft::UI::Composition::SystemBackdrops;
        const auto current = m_state->acrylic ? m_state->acrylic.State() : m_state->mica.State();
        const int value = static_cast<int>(current);
        if (value != m_state->lastState) {
            MaterialLog("Current state (0=Active,1=Fallback,2=HighContrast)", static_cast<HRESULT>(value));
            m_state->lastState = value;
        }
        return current == sb::SystemBackdropState::Active;
    } catch (const winrt::hresult_error& error) { MaterialLog("Read controller state", error.code()); }
    return false;
}
#else
struct CNativePlaylistMaterial::State {};
bool CNativePlaylistMaterial::Start(HWND, int, bool) { return false; }
bool CNativePlaylistMaterial::Active() const { return false; }
bool CNativePlaylistMaterial::UpdateForeground(const void*, int, int) { return false; }
bool CNativePlaylistMaterial::Ready() const { return false; }
bool CNativePlaylistMaterial::PrepareSlide(float) { return false; }
bool CNativePlaylistMaterial::StartSlide() { return false; }
bool CNativePlaylistMaterial::StartFade() { return false; }
void CNativePlaylistMaterial::FinishSlide() {}
void CNativePlaylistMaterial::SetVisible(bool) {}
#endif

CNativePlaylistMaterial::CNativePlaylistMaterial() = default;
CNativePlaylistMaterial::~CNativePlaylistMaterial() = default;
void CNativePlaylistMaterial::Reset() { m_state.reset(); }

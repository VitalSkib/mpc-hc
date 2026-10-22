// Isolated WinUI feasibility probe. Does not load or modify MPC-HC.
#define NOMINMAX
#include <windows.h>
#include <appmodel.h>
#include <fstream>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <microsoft.ui.interop.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
static Hosting::DesktopWindowXamlSource island{nullptr};
static std::ofstream logFile;
static void Log(const char* stage) { logFile << stage << std::endl; }
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_SIZE && island) island.SiteBridge().Resize({LOWORD(l), HIWORD(l)});
    if (message == WM_COMMAND) { logFile << "native-command " << LOWORD(w) << std::endl; return 0; }
    if (message == WM_CLOSE) { if (island) { island.Close(); island = nullptr; } DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}
static void CopyMenu(HMENU source, Windows::Foundation::Collections::IVector<MenuFlyoutItemBase> target, HWND owner) {
    // Same entry point a future MPC adapter would use for dynamic native state.
    SendMessageW(owner, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(source), 0);
    for (int i=0; i<GetMenuItemCount(source); ++i) {
        wchar_t caption[512]{}; MENUITEMINFOW item{sizeof(item)};
        item.fMask=MIIM_FTYPE|MIIM_STATE|MIIM_ID|MIIM_STRING|MIIM_SUBMENU;
        item.dwTypeData=caption; item.cch=_countof(caption);
        check_bool(GetMenuItemInfoW(source,i,TRUE,&item));
        if(item.fType&MFT_SEPARATOR) { target.Append(MenuFlyoutSeparator()); continue; }
        std::wstring text(caption), shortcut;
        if (auto at=text.find(L'\t'); at!=text.npos) { shortcut=text.substr(at+1); text.resize(at); }
        if(item.hSubMenu) {
            MenuFlyoutSubItem sub; sub.Text(text); sub.IsEnabled(!(item.fState&(MFS_DISABLED|MFS_GRAYED)));
            CopyMenu(item.hSubMenu,sub.Items(),owner); target.Append(sub);
        } else if(item.fState&MFS_CHECKED) {
            ToggleMenuFlyoutItem toggle; toggle.Text(text); toggle.IsChecked(true);
            toggle.IsEnabled(!(item.fState&(MFS_DISABLED|MFS_GRAYED)));
            toggle.Click([owner,id=item.wID](auto&&,auto&&){PostMessageW(owner,WM_COMMAND,id,0);});
            target.Append(toggle);
        } else {
            MenuFlyoutItem command; command.Text(text); command.KeyboardAcceleratorTextOverride(shortcut);
            command.IsEnabled(!(item.fState&(MFS_DISABLED|MFS_GRAYED)));
            command.Click([owner,id=item.wID](auto&&,auto&&){PostMessageW(owner,WM_COMMAND,id,0);});
            target.Append(command);
        }
    }
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR commandLine,int) {
    logFile.open("menu-probe.log",std::ios::trunc);
    const bool smoke = wcsstr(commandLine,L"--smoke") != nullptr;
    try {
        Log("start"); init_apartment(apartment_type::single_threaded);
        auto module=LoadLibraryW(L"Microsoft.WindowsAppRuntime.Bootstrap.dll"); check_bool(module!=nullptr);
        using Initialize=HRESULT(WINAPI*)(UINT32,PCWSTR,PACKAGE_VERSION);
        auto bootstrap=reinterpret_cast<Initialize>(GetProcAddress(module,"MddBootstrapInitialize")); check_bool(bootstrap!=nullptr);
        check_hresult(bootstrap(0x00010008,L"",PACKAGE_VERSION{})); Log("runtime-ready");
        auto queue=Microsoft::UI::Dispatching::DispatcherQueueController::CreateOnCurrentThread();
        auto xaml=Hosting::WindowsXamlManager::InitializeForCurrentThread(); Log("xaml-ready");
        WNDCLASSW wc{}; wc.lpfnWndProc=WindowProc; wc.hInstance=instance; wc.lpszClassName=L"MpcWinUIMenuProbe";
        wc.hCursor=LoadCursor(nullptr,IDC_ARROW); RegisterClassW(&wc);
        HWND window=CreateWindowW(wc.lpszClassName,L"MPC-HC: isolated WinUI menu probe",WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,CW_USEDEFAULT,640,400,nullptr,nullptr,instance,nullptr); check_bool(window!=nullptr);
        island=Hosting::DesktopWindowXamlSource();
        ABI::Microsoft::UI::WindowId id{}; check_hresult(ABI::Microsoft::UI::GetWindowIdFromWindow(window,&id));
        island.Initialize({id.Value}); island.SiteBridge().Resize({620,340});
        Button button; button.Content(box_value(L"Open native WinUI menu"));
        island.Content(button); Log("island-ready");
        HMENU menu=CreatePopupMenu(), sub=CreatePopupMenu();
        AppendMenuW(menu,MF_STRING,100,L"Play / Pause\tSpace");
        AppendMenuW(menu,MF_STRING|MF_CHECKED,101,L"Checked item");
        AppendMenuW(menu,MF_STRING|MF_GRAYED,102,L"Disabled item");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(sub,MF_STRING,103,L"Existing native command");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(sub),L"Submenu");
        MenuFlyout flyout; CopyMenu(menu,flyout.Items(),window); Log("menu-ready: 5 items; submenu/check/disabled/command bridge");
        button.Click([flyout,button](auto&&,auto&&){flyout.ShowAt(button);});
        if(smoke) { island.Close(); island=nullptr; DestroyWindow(window); }
        else {
            ShowWindow(window,SW_SHOWNORMAL); MSG msg;
            while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        DestroyMenu(menu); xaml.Close(); Log("PASS");
    } catch(const hresult_error& error) {
        logFile << "FAIL HRESULT=0x" << std::hex << static_cast<unsigned>(error.code().value) << std::endl;
        return 1;
    }
    return 0;
}

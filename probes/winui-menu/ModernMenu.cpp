// WinUI presentation adapter. Native HMENU and WM_COMMAND remain authoritative.
#define NOMINMAX
#include <windows.h>
#include <appmodel.h>
#include <memory>
#include <string>
#include <vector>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/microsoft.ui.interop.h>
#include <Microsoft.UI.Dispatching.Interop.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
struct MenuApplication : ApplicationT<MenuApplication, Markup::IXamlMetadataProvider> {
    XamlTypeInfo::XamlControlsXamlMetaDataProvider provider;
    Markup::IXamlType GetXamlType(hstring const& n) { return provider.GetXamlType(n); }
    Markup::IXamlType GetXamlType(Windows::UI::Xaml::Interop::TypeName const& n) { return provider.GetXamlType(n); }
    com_array<Markup::XmlnsDefinition> GetXmlnsDefinitions() { return provider.GetXmlnsDefinitions(); }
};
struct MenuHost {
    Microsoft::UI::Dispatching::DispatcherQueueController queue{nullptr};
    Application app{nullptr};
    Hosting::WindowsXamlManager xaml{nullptr};
    Hosting::DesktopWindowXamlSource island{nullptr};
    Grid root{nullptr}; MenuFlyout flyout{nullptr};
    bool active=false, closed=false, stopping=false;
    UINT selected=0;
    HWND owner=nullptr;
    void Close() noexcept {
        try { if(flyout)flyout.Hide(); }catch(...){}
        flyout=nullptr;root=nullptr;
        try { if(island)island.Close(); }catch(...){} island=nullptr;
        try { if(xaml)xaml.Close(); }catch(...){} xaml=nullptr;app=nullptr;
        try { if(queue)queue.ShutdownQueueAsync(); }catch(...){} queue=nullptr;
    }
    ~MenuHost(){Close();}
};
static std::unique_ptr<MenuHost> host;
static bool unavailable=false;
// Only strip mnemonic markers, retaining escaped literal ampersands.
static std::wstring Label(std::wstring s) {
    std::wstring r; for(size_t i=0;i<s.size();++i){
        if(s[i]!=L'&')r+=s[i];else if(i+1<s.size()&&s[i+1]==L'&'){r+=L'&';++i;}
    } return r;
}
static void CopyMenu(HMENU source, Windows::Foundation::Collections::IVector<MenuFlyoutItemBase> target,
    HWND owner, UINT position, unsigned depth, unsigned& count) {
    if(depth>12||count>2048)throw hresult_error(E_BOUNDS);
    SendMessageW(owner,WM_INITMENUPOPUP,reinterpret_cast<WPARAM>(source),MAKELPARAM(position,FALSE));
    const int n=GetMenuItemCount(source);if(n<0)throw hresult_error(E_INVALIDARG);
    for(int i=0;i<n;++i){
        if(++count>2048)throw hresult_error(E_BOUNDS);
        MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_FTYPE|MIIM_STATE|MIIM_ID|MIIM_SUBMENU|MIIM_STRING;
        check_bool(GetMenuItemInfoW(source,i,TRUE,&item));
        std::vector<wchar_t> caption(size_t(item.cch)+1);item.dwTypeData=caption.data();item.cch=UINT(caption.size());
        check_bool(GetMenuItemInfoW(source,i,TRUE,&item));
        if(item.fType&MFT_SEPARATOR){target.Append(MenuFlyoutSeparator());continue;}
        std::wstring text(caption.data()),shortcut;
        if(auto at=text.find(L'\t');at!=text.npos){shortcut=text.substr(at+1);text.resize(at);}
        text=Label(text);const bool enabled=!(item.fState&(MFS_DISABLED|MFS_GRAYED));
        if(item.hSubMenu){
            MenuFlyoutSubItem sub;sub.Text(text);sub.IsEnabled(enabled);
            // Disabled branches cannot be opened. Avoid querying inactive filters.
            if(enabled)CopyMenu(item.hSubMenu,sub.Items(),owner,UINT(i),depth+1,count);
            target.Append(sub);
        }else{
            MenuFlyoutItem command;command.Text(text);command.IsEnabled(enabled);
            command.KeyboardAcceleratorTextOverride(shortcut);
            // Native flags decide check/radio artwork; never toggle data in WinUI.
            if(item.fState&MFS_CHECKED){FontIcon icon;icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
                icon.Glyph(item.fType&MFT_RADIOCHECK?L"\xEA3A":L"\xE73E");command.Icon(icon);}
            command.Click([id=item.wID](auto&&,auto&&){if(host&&host->active){host->selected=id;host->flyout.Hide();}});
            target.Append(command);
        }
    }
    SendMessageW(owner,WM_UNINITMENUPOPUP,reinterpret_cast<WPARAM>(source),0);
}
static void Ensure(HWND owner) {
    if(host&&host->owner==owner)return;
    host.reset();auto state=std::make_unique<MenuHost>();state->owner=owner;
    auto q=Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    if(!q)state->queue=Microsoft::UI::Dispatching::DispatcherQueueController::CreateOnCurrentThread();
    state->app=make<MenuApplication>();
    state->xaml=Hosting::WindowsXamlManager::InitializeForCurrentThread();
    state->app.Resources().MergedDictionaries().Append(XamlControlsResources());
    state->island=Hosting::DesktopWindowXamlSource();state->island.Initialize(Microsoft::UI::GetWindowIdFromWindow(owner));
    state->root=Grid();state->root.RequestedTheme(ElementTheme::Dark);
    state->island.Content(state->root);host=std::move(state);
}
extern "C" __declspec(dllexport) void WINAPI MpcModernMenuShutdown() noexcept {
    if(host&&host->active){host->stopping=true;host->closed=true;try{host->flyout.Hide();}catch(...){}return;}
    host.reset();
}
extern "C" __declspec(dllexport) BOOL WINAPI MpcModernMenu(HWND owner,HMENU source,POINT screen,UINT* command) noexcept {
    if(command)*command=0;bool entered=false,shown=false;
    try{
        if(unavailable||!IsWindow(owner)||!IsMenu(source)||(host&&host->active))return FALSE;
        Ensure(owner);host->active=true;host->closed=false;host->selected=0;
        SendMessageW(owner,WM_ENTERMENULOOP,TRUE,0);entered=true;
        SendMessageW(owner,WM_INITMENU,reinterpret_cast<WPARAM>(source),0);
        host->flyout=MenuFlyout();unsigned count=0;CopyMenu(source,host->flyout.Items(),owner,0,0,count);
        host->flyout.SystemBackdrop(Media::DesktopAcrylicBackdrop());
        Style presenter(xaml_typename<MenuFlyoutPresenter>());
        presenter.Setters().Append(Setter(Control::BackgroundProperty(),Media::SolidColorBrush(Windows::UI::Color{0,0,0,0})));
        host->flyout.MenuFlyoutPresenterStyle(presenter);
        host->flyout.Closed([](auto&&,auto&&){if(host)host->closed=true;});
        POINT point=screen;ScreenToClient(owner,&point);
        host->island.SiteBridge().MoveAndResize({point.x,point.y,1,1});host->island.SiteBridge().Show();
        host->root.UpdateLayout();
        host->flyout.ShowAt(host->root);shown=true;
        MSG msg{};while(host&&!host->closed&&IsWindow(owner)){
            int result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(int(msg.wParam));break;}
            if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){host->flyout.Hide();continue;}
            if(!ContentPreTranslateMessage(&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        if(host){if(command)*command=host->selected;host->flyout=nullptr;host->island.SiteBridge().Hide();host->active=false;}
        if(IsWindow(owner)){SendMessageW(owner,WM_MENUSELECT,MAKEWPARAM(0,0xFFFF),0);SendMessageW(owner,WM_EXITMENULOOP,TRUE,0);entered=false;}
        if(host&&host->stopping)host.reset();
        return TRUE;
    }catch(...){
        unavailable=true;
        if(host){host->active=false;host->Close();host.reset();}
        if(entered&&IsWindow(owner))SendMessageW(owner,WM_EXITMENULOOP,TRUE,0);
        return shown?TRUE:FALSE;
    }
}


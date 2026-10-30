// Installer prerequisite probe. Uses the same official bootstrap API as the player; no UI.
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <appmodel.h>
#include <roapi.h>
#include <activation.h>
#include <cstdio>
#pragma comment(lib,"runtimeobject.lib")

int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    const auto module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module)return 1;
    using Initialize=HRESULT(WINAPI*)(UINT32,PCWSTR,PACKAGE_VERSION);
    using Shutdown=void(WINAPI*)();
    const auto initialize=reinterpret_cast<Initialize>(GetProcAddress(module,"MddBootstrapInitialize"));
    const auto shutdown=reinterpret_cast<Shutdown>(GetProcAddress(module,"MddBootstrapShutdown"));
    if(!initialize||!shutdown){FreeLibrary(module);return 1;}
    HRESULT hr=initialize(0x00010008,L"",PACKAGE_VERSION{});
    if(SUCCEEDED(hr)) {
        const HRESULT apartment=RoInitialize(RO_INIT_SINGLETHREADED);
        if(SUCCEEDED(apartment)) {
            constexpr wchar_t name[]=L"Microsoft.UI.Composition.SystemBackdrops.DesktopAcrylicController";
            HSTRING className=nullptr;
            hr=WindowsCreateString(name,static_cast<UINT32>(_countof(name)-1),&className);
            if(SUCCEEDED(hr)) {
                IActivationFactory* factory=nullptr;
                hr=RoGetActivationFactory(className,IID_PPV_ARGS(&factory));
                if(factory)factory->Release();
                WindowsDeleteString(className);
            }
            RoUninitialize();
        } else hr=apartment;
        shutdown();
    }
    FreeLibrary(module);
    if(FAILED(hr))fwprintf(stderr,L"Windows App Runtime 1.8 probe: 0x%08X\n",static_cast<unsigned>(hr));
    return SUCCEEDED(hr)?0:1;
}

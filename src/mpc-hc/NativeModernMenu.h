#pragma once
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
#include "wasdk-probe/modern-menu-bytes.h"
namespace NativeModernMenu {
using ShowFunction=BOOL(WINAPI*)(HWND,HMENU,POINT,UINT*);
using StopFunction=void(WINAPI*)();
inline HMODULE& Module(){static HMODULE module=nullptr;return module;}
inline bool Load(){
    if(Module())return true;
    // Composition initialises the installed App SDK before this optional adapter.
    // Do not resolve imports or alter global runtime policy when it is unavailable.
    if(!GetModuleHandleW(L"Microsoft.WindowsAppRuntime.Bootstrap.dll"))return false;
    wchar_t temp[MAX_PATH]{};if(!GetTempPathW(MAX_PATH,temp))return false;
    std::wstring folder=std::wstring(temp)+L"MPC-HC-ModernMenu-"+std::to_wstring(GetCurrentProcessId());
    if(!CreateDirectoryW(folder.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    std::wstring file=folder+L"\\ModernMenu.dll";
    HANDLE f=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;BOOL ok=WriteFile(f,kModernMenuAdapter,sizeof(kModernMenuAdapter),&written,nullptr);CloseHandle(f);
    if(!ok||written!=sizeof(kModernMenuAdapter)){DeleteFileW(file.c_str());RemoveDirectoryW(folder.c_str());return false;}
    Module()=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!Module()){DeleteFileW(file.c_str());RemoveDirectoryW(folder.c_str());return false;}
    // Mark for deletion when the loader releases its image at process termination.
    HANDLE cleanup=CreateFileW(file.c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if(cleanup!=INVALID_HANDLE_VALUE)CloseHandle(cleanup);
    RemoveDirectoryW(folder.c_str());
    return true;
}
inline bool Show(HWND owner,HMENU menu,POINT point){
    if(!Load())return false;
    auto show=reinterpret_cast<ShowFunction>(GetProcAddress(Module(),"MpcModernMenu"));
    if(!show)return false;
    UINT command=0;const BOOL handled=show(owner,menu,point,&command);
    if(handled&&command&&IsWindow(owner))PostMessageW(owner,WM_COMMAND,command,0);
    return !!handled;
}
inline void Shutdown(){if(Module()){
    auto stop=reinterpret_cast<StopFunction>(GetProcAddress(Module(),"MpcModernMenuShutdown"));if(stop)stop();
    // WinRT activation callbacks may retain code addresses until process exit.
    // The image stays loaded; all island, flyout and queue objects are released.
}}
}
#endif

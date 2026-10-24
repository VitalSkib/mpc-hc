#pragma once
#include <windows.h>
#include <commctrl.h>
namespace NativeHeaderInput {
inline bool Resize(LRESULT hit) { return hit>=HTLEFT && hit<=HTBOTTOMRIGHT; }
inline LRESULT Hit(LRESULT frame, bool button) { return Resize(frame)?HTTRANSPARENT:(button?HTCLIENT:frame); }
inline bool Action(UINT message) {
    return message==WM_NCLBUTTONDOWN || message==WM_NCLBUTTONUP || message==WM_NCLBUTTONDBLCLK
        || message==WM_NCRBUTTONDOWN || message==WM_NCRBUTTONUP || message==WM_NCRBUTTONDBLCLK
        || message==WM_NCMBUTTONDOWN || message==WM_NCMBUTTONUP || message==WM_NCMBUTTONDBLCLK;
}
inline LRESULT Forward(HWND frame, UINT message, WPARAM hit, LPARAM point) {
    return ::SendMessageW(frame,message,hit,point);
}
// A chrome child is not the frame. Leave resize input to the original video
// view, whose stock nonclient handler already starts SC_SIZE correctly.
inline UINT_PTR BorderSubclassId();
inline void AttachBorder(HWND window, HWND view);
inline LRESULT CALLBACK BorderProcedure(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
    const HWND view=reinterpret_cast<HWND>(data);
    if(message==WM_NCDESTROY) ::RemoveWindowSubclass(window,BorderProcedure,id);
    else if(message==WM_NCHITTEST && ::IsWindow(view) && window!=view) {
        if(Resize(::SendMessageW(view,WM_NCHITTEST,0,l))) return HTTRANSPARENT;
    } else if(message==WM_PARENTNOTIFY && LOWORD(w)==WM_CREATE)
        AttachBorder(reinterpret_cast<HWND>(l),view);
    return ::DefSubclassProc(window,message,w,l);
}
inline UINT_PTR BorderSubclassId(){return reinterpret_cast<UINT_PTR>(BorderProcedure);}
inline BOOL CALLBACK AttachChild(HWND child,LPARAM view) {
    ::SetWindowSubclass(child,BorderProcedure,BorderSubclassId(),static_cast<DWORD_PTR>(view));return TRUE;
}
inline void AttachBorder(HWND window,HWND view) {
    if(!::IsWindow(window)||!::IsWindow(view)) return;
    ::SetWindowSubclass(window,BorderProcedure,BorderSubclassId(),reinterpret_cast<DWORD_PTR>(view));
    ::EnumChildWindows(window,AttachChild,reinterpret_cast<LPARAM>(view));
}
inline BOOL CALLBACK DetachChild(HWND child,LPARAM) {
    ::RemoveWindowSubclass(child,BorderProcedure,BorderSubclassId());return TRUE;
}
inline void DetachBorder(HWND window) {
    if(!::IsWindow(window))return;
    ::EnumChildWindows(window,DetachChild,0);
    ::RemoveWindowSubclass(window,BorderProcedure,BorderSubclassId());
}

}

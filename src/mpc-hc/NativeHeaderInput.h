#pragma once
#include <windows.h>
namespace NativeHeaderInput {
inline bool Resize(LRESULT hit) { return hit>=HTLEFT && hit<=HTBOTTOMRIGHT; }
inline LRESULT Hit(LRESULT frame, bool button) { return Resize(frame)?frame:(button?HTCLIENT:frame); }
inline bool Action(UINT message) {
    return message==WM_NCLBUTTONDOWN || message==WM_NCLBUTTONUP || message==WM_NCLBUTTONDBLCLK
        || message==WM_NCRBUTTONDOWN || message==WM_NCRBUTTONUP || message==WM_NCRBUTTONDBLCLK
        || message==WM_NCMBUTTONDOWN || message==WM_NCMBUTTONUP || message==WM_NCMBUTTONDBLCLK;
}
inline LRESULT Forward(HWND frame, UINT message, WPARAM hit, LPARAM point) {
    return ::SendMessageW(frame,message,hit,point);
}
}

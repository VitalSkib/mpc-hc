#define UNICODE
#define _UNICODE
#include "NativeHeaderInput.h"
#include <iostream>
#include <cstdlib>
static int checks=0;
static UINT seenMessage=0;static WPARAM seenHit=0;static LPARAM seenPoint=0;
static void check(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}}
static int widgetHits=0;
static LRESULT CALLBACK Widget(HWND w,UINT m,WPARAM p,LPARAM l){
    if(m==WM_NCHITTEST){++widgetHits;return HTCLIENT;}
    if(m==WM_APP+1)return 777;
    return DefWindowProcW(w,m,p,l);
}
static LRESULT CALLBACK Frame(HWND w,UINT m,WPARAM hit,LPARAM p){
    if(NativeHeaderInput::Action(m)){seenMessage=m;seenHit=hit;seenPoint=p;return 123;}
    return DefWindowProcW(w,m,hit,p);
}
int main(){
    HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=Frame;c.lpszClassName=L"NativeHeaderInputRegression";
    check(RegisterClassW(&c)!=0,"register frame");
    HWND frame=CreateWindowExW(0,c.lpszClassName,L"Frame input test",WS_OVERLAPPEDWINDOW,
        100,100,600,400,nullptr,nullptr,instance,nullptr);
    check(frame!=nullptr,"create native frame");
    WNDCLASSW childClass=c;childClass.lpfnWndProc=Widget;childClass.lpszClassName=L"NativeChromeInputWidget";
    check(RegisterClassW(&childClass)!=0,"register overlay widget");
    HWND overlay=CreateWindowW(childClass.lpszClassName,L"",WS_CHILD,0,0,600,400,frame,nullptr,instance,nullptr);
    HWND child=CreateWindowW(childClass.lpszClassName,L"",WS_CHILD,0,0,100,100,overlay,nullptr,instance,nullptr);
    check(overlay && child,"create overlay and existing native descendant");
    NativeHeaderInput::AttachBorder(overlay,frame);
    HWND lateChild=CreateWindowW(childClass.lpszClassName,L"",WS_CHILD,0,0,100,100,overlay,nullptr,instance,nullptr);
    check(lateChild!=nullptr,"create descendant after adopting overlay");
    RECT r{};GetWindowRect(frame,&r);
    const POINT points[]={{r.left+1,r.top+1},{(r.left+r.right)/2,r.top+1},{r.right-2,r.top+1},
        {r.left+1,(r.top+r.bottom)/2},{r.right-2,(r.top+r.bottom)/2},
        {r.left+1,r.bottom-2},{(r.left+r.right)/2,r.bottom-2},{r.right-2,r.bottom-2}};
    for(auto p:points){
        const LPARAM position=MAKELPARAM(p.x,p.y);
        const auto native=SendMessageW(frame,WM_NCHITTEST,0,position);
        check(NativeHeaderInput::Resize(native),"system frame identifies resize edge or corner");
        check(NativeHeaderInput::Hit(native,false)==HTTRANSPARENT,"header leaves resize edge to stock view");
        check(NativeHeaderInput::Hit(native,true)==HTTRANSPARENT,"resize border passes through caption button overlap");
        for(HWND widget:{overlay,child,lateChild})
            check(SendMessageW(widget,WM_NCHITTEST,0,position)==HTTRANSPARENT,
                "adopted controls and descendants leave resize edge to stock view");
    }
    check(NativeHeaderInput::Hit(HTCAPTION,false)==HTCAPTION,"caption stays nonclient");
    check(NativeHeaderInput::Hit(HTCAPTION,true)==HTCLIENT,"custom window buttons keep client input");
    check(NativeHeaderInput::Hit(HTCLIENT,false)==HTCLIENT,"fullscreen background remains a client sink");
    const LPARAM signedPosition=MAKELPARAM(-1200,-300);
    check(NativeHeaderInput::Forward(frame,WM_NCLBUTTONDBLCLK,HTCAPTION,signedPosition)==123
        && seenMessage==WM_NCLBUTTONDBLCLK && seenPoint==signedPosition,"native double click and signed coordinates are retained");
    check(!NativeHeaderInput::Action(WM_LBUTTONUP),"client/video clicks are not sent through nonclient forwarding");
    const LPARAM centre=MAKELPARAM((r.left+r.right)/2,(r.top+r.bottom)/2);
    check(SendMessageW(child,WM_NCHITTEST,0,centre)==HTCLIENT && widgetHits>0,
        "ordinary widget client input stays native");
    check(SendMessageW(child,WM_APP+1,0,0)==777,"unrelated native widget messages are unchanged");
    NativeHeaderInput::DetachBorder(overlay);
    check(SendMessageW(child,WM_NCHITTEST,0,MAKELPARAM(r.left+1,r.top+1))==HTCLIENT,
        "restoring native controls removes border routing");
    DestroyWindow(frame);
    std::cout<<"PASS "<<checks<<" native header frame input checks\n";
}

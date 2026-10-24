#define UNICODE
#define _UNICODE
#include "NativeHeaderInput.h"
#include <iostream>
#include <cstdlib>
static int checks=0;
static UINT seenMessage=0;static WPARAM seenHit=0;static LPARAM seenPoint=0;
static void check(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}}
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
    RECT r{};GetWindowRect(frame,&r);
    const POINT points[]={{r.left+1,r.top+1},{(r.left+r.right)/2,r.top+1},{r.right-2,r.top+1},
        {r.left+1,(r.top+r.bottom)/2},{r.right-2,(r.top+r.bottom)/2},
        {r.left+1,r.bottom-2},{(r.left+r.right)/2,r.bottom-2},{r.right-2,r.bottom-2}};
    for(auto p:points){
        const LPARAM position=MAKELPARAM(p.x,p.y);
        const auto native=SendMessageW(frame,WM_NCHITTEST,0,position);
        check(NativeHeaderInput::Resize(native),"system frame identifies resize edge or corner");
        check(NativeHeaderInput::Hit(native,false)==native,"header keeps native resize hit");
        check(NativeHeaderInput::Hit(native,true)==native,"resize border wins over caption button overlap");
        check(NativeHeaderInput::Forward(frame,WM_NCLBUTTONDOWN,native,position)==123
            && seenHit==static_cast<WPARAM>(native)&&seenPoint==position,"native resize input reaches frame unchanged");
    }
    check(NativeHeaderInput::Hit(HTCAPTION,false)==HTCAPTION,"caption stays nonclient");
    check(NativeHeaderInput::Hit(HTCAPTION,true)==HTCLIENT,"custom window buttons keep client input");
    check(NativeHeaderInput::Hit(HTCLIENT,false)==HTCLIENT,"fullscreen background remains a client sink");
    const LPARAM signedPosition=MAKELPARAM(-1200,-300);
    check(NativeHeaderInput::Forward(frame,WM_NCLBUTTONDBLCLK,HTCAPTION,signedPosition)==123
        && seenMessage==WM_NCLBUTTONDBLCLK && seenPoint==signedPosition,"native double click and signed coordinates are retained");
    check(!NativeHeaderInput::Action(WM_LBUTTONUP),"client/video clicks are not sent through nonclient forwarding");
    DestroyWindow(frame);
    std::cout<<"PASS "<<checks<<" native header frame input checks\n";
}

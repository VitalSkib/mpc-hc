#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <iostream>
#include <cstdlib>
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
static int draws=0;
static void check(bool condition,const char* label) { if(!condition){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);} }
static LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_MEASUREITEM){reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight=24;return TRUE;}
    if(message==WM_DRAWITEM){++draws;return TRUE;}
    return DefWindowProc(window,message,w,l);
}
int main() {
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_LISTVIEW_CLASSES};check(InitCommonControlsEx(&init),"common controls");
    HINSTANCE instance=GetModuleHandle(nullptr);
    WNDCLASS wc{};wc.hInstance=instance;wc.lpfnWndProc=Procedure;wc.lpszClassName=L"PlaylistScrollBatchTest";
    check(RegisterClass(&wc)!=0,"test class");
    HWND parent=CreateWindow(wc.lpszClassName,L"Scroll batching test",WS_OVERLAPPEDWINDOW,-10000,-10000,500,300,nullptr,nullptr,instance,nullptr);
    check(parent!=nullptr,"parent");
    HWND list=CreateWindow(WC_LISTVIEW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_OWNERDRAWFIXED,0,0,450,220,parent,reinterpret_cast<HMENU>(1),instance,nullptr);
    check(list!=nullptr,"list");
    LVCOLUMN column{};column.mask=LVCF_WIDTH|LVCF_TEXT;column.cx=420;wchar_t title[]=L"Programme";column.pszText=title;
    check(ListView_InsertColumn(list,0,&column)>=0,"column");
    for(int i=0;i<80;++i){LVITEM item{};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=title;check(ListView_InsertItem(list,&item)>=0,"row");}
    ShowWindow(parent,SW_SHOWNOACTIVATE);UpdateWindow(list);
    const int before=ListView_GetTopIndex(list), beforeDraws=draws;
    SendMessage(list,WM_SETREDRAW,FALSE,0);
    check(ListView_Scroll(list,0,240),"native scrolling while redraw deferred");
    const int after=ListView_GetTopIndex(list);
    check(after>before,"native top index advances");
    check(draws==beforeDraws,"no intermediate owner draw");
    check(ListView_GetItemCount(list)==80,"items unchanged");
    SendMessage(list,WM_SETREDRAW,TRUE,0);
    check(IsWindowVisible(list),"native visibility restored");
    HDC dc=GetDC(list),memory=CreateCompatibleDC(dc);
    HBITMAP bitmap=CreateCompatibleBitmap(dc,450,220);HGDIOBJ old=SelectObject(memory,bitmap);
    const int beforePrint=draws;
    SendMessage(list,WM_PRINT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);
    check(draws>beforePrint,"explicit foreground snapshot still draws native rows");
    check(ListView_GetTopIndex(list)==after,"snapshot preserves scroll position");
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(list,dc);
    ListView_SetItemState(list, after, LVIS_SELECTED, LVIS_SELECTED);
    HWND composition = CreateWindowEx(WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName,
        L"Composition host", WS_POPUP, -10000, -10000, 500, 300, nullptr, nullptr, instance, nullptr);
    check(composition != nullptr, "composition host creation");
    check((GetWindowLongPtr(composition, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0,
        "creation-time no-redirection mode is real");
    SetParent(list, composition);
    check(ListView_GetItemCount(list) == 80 && ListView_GetTopIndex(list) == after,
        "moving native list to composition host preserves content and scroll");
    check(ListView_GetItemState(list, after, LVIS_SELECTED) != 0,
        "moving native list preserves selection");
    DestroyWindow(parent);
    HWND solid = CreateWindowEx(0, wc.lpszClassName, L"Solid fallback", WS_POPUP,
        -10000, -10000, 500, 300, nullptr, nullptr, instance, nullptr);
    check(solid != nullptr && !(GetWindowLongPtr(solid, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP),
        "solid fallback has its ordinary redirection surface");
    SetParent(list, solid);
    DestroyWindow(composition);
    check(IsWindow(list) && ListView_GetItemCount(list) == 80 && ListView_GetTopIndex(list) == after,
        "recreating the host keeps the native list alive with its scroll state");
    check(ListView_GetItemState(list, after, LVIS_SELECTED) != 0,
        "solid fallback preserves native selection");
    DestroyWindow(solid);
    std::cout<<"PASS native scroll state, deferred drawing, visibility and explicit print\n";
}

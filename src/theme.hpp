#pragma once
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <string>
#include <gdiplus.h>
#include <cmath>

// Native controls retain keyboard navigation and accessibility semantics.
namespace quiet::ui {
inline int preference = 0; // system, light, dark
inline bool dark = false, highContrast = false;
inline COLORREF background, surface, foreground, muted, accent, border;
inline HBRUSH backgroundBrush = nullptr, surfaceBrush = nullptr;
inline ULONG_PTR graphicsToken = 0;
inline int px(HWND window,int value) { return MulDiv(value,GetDpiForWindow(window),96); }
inline LRESULT CALLBACK fontLifetime(HWND control,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    if(msg==WM_NCDESTROY) {
        auto handle=RemovePropW(control,L"QuietFont");
        auto result=DefSubclassProc(control,msg,w,l);
        if(handle) DeleteObject(handle);
        return result;
    }
    return DefSubclassProc(control,msg,w,l);
}
inline void font(HWND control,int size,int weight=FW_NORMAL) {
    if(!control) return;
    HFONT previous=reinterpret_cast<HFONT>(GetPropW(control,L"QuietFont"));
    HFONT next=CreateFontW(-px(control,size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);
    SetPropW(control,L"QuietFont",next);if(previous) DeleteObject(previous);
    SetWindowSubclass(control,fontLifetime,2,0);
}
inline void rounded(HDC dc,RECT r,COLORREF fill,COLORREF outline,int radius) {
    using namespace Gdiplus;
    Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);
    auto color=[](COLORREF c) { return Color(255,GetRValue(c),GetGValue(c),GetBValue(c)); };
    REAL x=static_cast<REAL>(r.left)+.5f,y=static_cast<REAL>(r.top)+.5f;
    REAL width=static_cast<REAL>(r.right-r.left)-1,height=static_cast<REAL>(r.bottom-r.top)-1;
    REAL d=static_cast<REAL>((std::min)(radius,static_cast<int>((std::min)(width,height))));
    if(d<1) return;
    GraphicsPath path;path.AddArc(x,y,d,d,180,90);path.AddArc(x+width-d,y,d,d,270,90);
    path.AddArc(x+width-d,y+height-d,d,d,0,90);path.AddArc(x,y+height-d,d,d,90,90);path.CloseFigure();
    SolidBrush brush(color(fill));Pen pen(color(outline),1);g.FillPath(&brush,&path);g.DrawPath(&pen,&path);
}
inline void icon(HDC dc,int x,int y,int size,COLORREF tint,int kind) {
    using namespace Gdiplus;Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.TranslateTransform(static_cast<REAL>(x),static_cast<REAL>(y));g.ScaleTransform(size/24.f,size/24.f);
    Color color(255,GetRValue(tint),GetGValue(tint),GetBValue(tint));Pen pen(color,1.8f);
    pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);SolidBrush brush(color);
    if(kind==0) { // headphone mark
        g.DrawArc(&pen,4.f,3.f,16.f,17.f,180.f,180.f);
        g.FillRectangle(&brush,3.f,11.f,5.f,9.f);g.FillRectangle(&brush,16.f,11.f,5.f,9.f);
    } else if(kind==1) { // settings
        g.DrawEllipse(&pen,5.f,5.f,14.f,14.f);g.DrawEllipse(&pen,9.f,9.f,6.f,6.f);
        for(int i=0;i<8;++i) { double a=i*3.141592653589793/4;
            g.DrawLine(&pen,12.f+static_cast<float>(7*cos(a)),12.f+static_cast<float>(7*sin(a)),
                12.f+static_cast<float>(10*cos(a)),12.f+static_cast<float>(10*sin(a))); }
    } else if(kind==2) { g.DrawLine(&pen,7.f,7.f,17.f,17.f);g.DrawLine(&pen,17.f,7.f,7.f,17.f); }
    else { g.DrawLine(&pen,9.f,6.f,15.f,12.f);g.DrawLine(&pen,15.f,12.f,9.f,18.f); }
}
inline void place(HWND dialog,int id,int x,int y,int width,int height) {
    SetWindowPos(GetDlgItem(dialog,id),nullptr,px(dialog,x),px(dialog,y),px(dialog,width),px(dialog,height),SWP_NOZORDER);
}
// Build a complete control frame in memory. Neither background clearing nor
// intermediate GDI/GDI+ drawing is presented to the desktop on its own.
class ControlPaint {
    HWND control_;
    bool painting_;
    PAINTSTRUCT paint_{};
    HDC target_=nullptr, memory_=nullptr;
    HBITMAP bitmap_=nullptr;
    HGDIOBJ previous_=nullptr;
public:
    RECT rect{};
    ControlPaint(HWND control,UINT message,WPARAM target):control_(control),painting_(message==WM_PAINT) {
        target_=painting_?BeginPaint(control_,&paint_):reinterpret_cast<HDC>(target);
        GetClientRect(control_,&rect);
        if(target_ && rect.right>0 && rect.bottom>0) {
            memory_=CreateCompatibleDC(target_);
            bitmap_=CreateCompatibleBitmap(target_,rect.right,rect.bottom);
            if(memory_ && bitmap_) previous_=SelectObject(memory_,bitmap_);
        }
    }
    ControlPaint(const ControlPaint&)=delete;
    ControlPaint& operator=(const ControlPaint&)=delete;
    HDC dc() const { return previous_?memory_:target_; }
    ~ControlPaint() {
        if(previous_) {
            BitBlt(target_,0,0,rect.right,rect.bottom,memory_,0,0,SRCCOPY);
            SelectObject(memory_,previous_);
        }
        if(bitmap_) DeleteObject(bitmap_);
        if(memory_) DeleteDC(memory_);
        if(painting_) EndPaint(control_,&paint_);
    }
};
inline LRESULT CALLBACK hotkeyPaint(HWND control,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_PAINT || msg==WM_PRINTCLIENT || msg==WM_PRINT) {
        ControlPaint paint(control,msg,w);HDC dc=paint.dc();RECT r=paint.rect;
        FillRect(dc,&r,backgroundBrush);rounded(dc,r,surface,GetFocus()==control?accent:border,px(control,12));
        auto oldFont=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)));
        auto key=static_cast<UINT>(SendMessageW(control,HKM_GETHOTKEY,0,0));std::wstring label;
        if(key&0xff) {
            if(key&0x200) label+=L"Ctrl + ";if(key&0x400) label+=L"Alt + ";if(key&0x100) label+=L"Shift + ";
            LONG bits=static_cast<LONG>(MapVirtualKeyW(key&0xff,MAPVK_VK_TO_VSC)<<16);
            if(key&0x800) bits|=1<<24;wchar_t name[64]{};
            if(GetKeyNameTextW(bits,name,64)) label+=name;else label+=L"?";
        } else {
            auto hint=reinterpret_cast<const wchar_t*>(GetPropW(control,L"QuietHint"));label=hint?hint:L"Press a shortcut";
        }
        SetTextColor(dc,(key&0xff)?foreground:muted);SetBkMode(dc,TRANSPARENT);r.left+=px(control,12);
        DrawTextW(dc,label.c_str(),-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        SelectObject(dc,oldFont);return 0;
    }
    auto result=DefSubclassProc(control,msg,w,l);
    // This control uses a focus border, not the native blinking insertion caret.
    if(msg==WM_SETFOCUS) DestroyCaret();
    if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS || msg==WM_KEYDOWN || msg==WM_KEYUP ||
        msg==WM_SYSKEYDOWN || msg==WM_SYSKEYUP || msg==HKM_SETHOTKEY)
        InvalidateRect(control,nullptr,FALSE);
    return result;
}
inline LRESULT CALLBACK inputPaint(HWND control,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR kind) {
    if(msg==WM_ERASEBKGND) return 1;
    // These controls have no hover appearance. Do not let the native class paint
    // its own hot-tracking over our surface. Preserve captured slider drags and
    // the combo's open-list interaction.
    if(msg==WM_MOUSEMOVE || msg==WM_MOUSELEAVE) {
        bool interacting=kind==1?GetCapture()==control:SendMessageW(control,CB_GETDROPPEDSTATE,0,0)!=FALSE;
        if(!interacting) return 0;
    }
    if(msg==WM_PAINT || msg==WM_PRINTCLIENT || msg==WM_PRINT) {
        ControlPaint paint(control,msg,w);HDC dc=paint.dc();RECT r=paint.rect;FillRect(dc,&r,backgroundBrush);
        bool focused=GetFocus()==control && !(SendMessageW(control,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS);
        if(kind==1) {
            RECT channel{},thumb{};SendMessageW(control,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
            SendMessageW(control,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
            // Trackbar geometry is supplied by the native control, including DPI.
            int center=r.right/2;RECT track{center-px(control,2),px(control,8),center+px(control,2),r.bottom-px(control,8)};
            rounded(dc,track,surface,surface,px(control,4));
            int pos=static_cast<int>(SendMessageW(control,TBM_GETPOS,0,0));
            int y=track.top+(track.bottom-track.top)*pos/20;
            if(thumb.bottom>thumb.top) y=(thumb.top+thumb.bottom)/2;
            COLORREF trackColor=IsWindowEnabled(control)?accent:muted;
            RECT active{track.left,y,track.right,track.bottom};rounded(dc,active,trackColor,trackColor,px(control,4));
            int radius=px(control,7);RECT knob{center-radius,y-radius,center+radius,y+radius};
            COLORREF color=IsWindowEnabled(control)?accent:muted;
            rounded(dc,knob,color,color,2*radius);
            if(focused) { InflateRect(&r,-1,-1);DrawFocusRect(dc,&r); }
        } else {
            rounded(dc,r,surface,focused?accent:border,px(control,12));
            wchar_t text[256]{};GetWindowTextW(control,text,256);
            auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)));
            SetTextColor(dc,IsWindowEnabled(control)?foreground:muted);SetBkMode(dc,TRANSPARENT);
            RECT label=r;label.left+=px(control,12);label.right-=px(control,32);
            DrawTextW(dc,text,-1,&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            SelectObject(dc,old);
            Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::Pen pen(Gdiplus::Color(255,GetRValue(muted),GetGValue(muted),GetBValue(muted)),static_cast<float>(px(control,1)));
            int x=r.right-px(control,18),y=r.bottom/2;
            g.DrawLine(&pen,x-px(control,4),y-px(control,2),x,y+px(control,2));
            g.DrawLine(&pen,x,y+px(control,2),x+px(control,4),y-px(control,2));
        }
        return 0;
    }
    auto result=DefSubclassProc(control,msg,w,l);
    if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS || msg==WM_ENABLE || msg==TBM_SETPOS ||
        msg==CB_SETCURSEL || msg==WM_KEYDOWN || msg==WM_MOUSEMOVE || msg==WM_LBUTTONDOWN ||
        msg==WM_LBUTTONUP || msg==WM_MOUSEWHEEL || msg==WM_TIMER || msg==WM_UPDATEUISTATE)
        InvalidateRect(control,nullptr,FALSE);
    return result;
}
inline void prepare(HWND dialog,bool panel=false) {
    // CAPTION in an RC template can add WS_CAPTION implicitly. Remove the actual
    // runtime non-client styles, then force Windows to recalculate the frame.
    auto style=GetWindowLongPtrW(dialog,GWL_STYLE);
    SetWindowLongPtrW(dialog,GWL_STYLE,(style&~(WS_CAPTION|WS_BORDER|WS_DLGFRAME|WS_THICKFRAME|DS_MODALFRAME))|WS_CLIPCHILDREN);
    auto ex=GetWindowLongPtrW(dialog,GWL_EXSTYLE);
    SetWindowLongPtrW(dialog,GWL_EXSTYLE,(ex&~(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE))|WS_EX_COMPOSITED);
    bool settings=GetDlgItem(dialog,IDC_SETTINGS_DEVICE)!=nullptr;
    bool hotkeys=GetDlgItem(dialog,IDC_HOTKEY_ANC)!=nullptr;
    int width=panel?320:settings?380:hotkeys?400:440;
    int height=panel?238:settings?488:hotkeys?500:436;
    SetWindowPos(dialog,nullptr,0,0,px(dialog,width),px(dialog,height),SWP_NOMOVE|SWP_NOZORDER|SWP_FRAMECHANGED);
    if(!panel) {
        POINT p{};GetCursorPos(&p);MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST),&monitor);
        SetWindowPos(dialog,nullptr,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-px(dialog,width))/2,
            monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-px(dialog,height))/2,0,0,SWP_NOSIZE|SWP_NOZORDER);
        CreateWindowExW(0,L"Static",L"S-XMTray",WS_CHILD|WS_VISIBLE,px(dialog,24),px(dialog,20),px(dialog,width-90),
            px(dialog,27),dialog,reinterpret_cast<HMENU>(IDC_UI_TITLE),GetModuleHandleW(nullptr),nullptr);
        CreateWindowExW(0,L"Button",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP, px(dialog,width-48),px(dialog,18),
            px(dialog,28),px(dialog,28),dialog,reinterpret_cast<HMENU>(IDC_UI_CLOSE),GetModuleHandleW(nullptr),nullptr);
    }
    if(panel) {
        place(dialog,IDC_PANEL_TITLE,52,20,235,22);place(dialog,IDC_PANEL_STATUS,52,44,244,18);
        ShowWindow(GetDlgItem(dialog,IDC_UI_CLOSE),SW_HIDE);
        place(dialog,IDC_PANEL_BATTERY,20,78,88,18);place(dialog,IDC_PANEL_RIGHT,112,78,88,18);
        place(dialog,IDC_PANEL_CASE,204,78,96,18);place(dialog,IDC_PANEL_NOTE,20,98,280,15);
        place(dialog,IDC_PANEL_NOISE,20,128,280,16);
        for(int i=0;i<3;++i) place(dialog,101+i,23+92*i,153,90,32);
        place(dialog,IDC_PANEL_SETTINGS,16,201,288,36);
    } else if(settings) {
        place(dialog,IDC_SETTINGS_DEVICE,24,80,332,16);
        place(dialog,130,24,104,332,32);place(dialog,131,24,142,332,32);
        place(dialog,132,24,184,332,36);place(dialog,121,24,226,332,36);
        place(dialog,IDC_SETTINGS_APPEARANCE,24,282,332,16);
        for(int i=0;i<3;++i) place(dialog,140+i,24+112*i,306,108,32);
        place(dialog,IDC_SETTINGS_LANGUAGE,24,352,332,16);
        for(int i=0;i<3;++i) place(dialog,110+i,24+112*i,376,108,32);
        place(dialog,120,24,422,332,32);
        // More vertical space for diagnostics and explicit Exit, below preferences.
        height=560;SetWindowPos(dialog,nullptr,0,0,px(dialog,width),px(dialog,height),SWP_NOMOVE|SWP_NOZORDER);
        place(dialog,IDC_SETTINGS_STATUS,24,464,332,30);
        place(dialog,104,20,510,112,32);place(dialog,106,136,510,108,32);place(dialog,105,248,510,108,32);
    } else if(hotkeys) {
        place(dialog,IDC_HOTKEY_ENABLE,24,82,352,26);
        for(int i=0;i<3;++i) {
            place(dialog,IDC_LABEL_ANC+i,24,128+72*i,352,18);
            place(dialog,IDC_HOTKEY_ANC+i,24,152+72*i,266,36);
            place(dialog,IDC_CLEAR_ANC+i,300,152+72*i,76,36);
        }
        place(dialog,IDC_HOTKEY_HINT,24,350,352,44);place(dialog,IDC_HOTKEY_ERROR,24,397,352,34);
        place(dialog,IDCANCEL,164,448,92,36);place(dialog,IDOK,264,448,112,36);
    } else {
        place(dialog,IDC_EQ_PRESET_LABEL,24,84,66,18);place(dialog,IDC_EQ_PRESET,100,78,316,200);
        place(dialog,IDC_EQ_HINT,24,112,392,40);
        for(int i=0;i<6;++i) {
            place(dialog,IDC_EQ_NAME_FIRST+i,22+66*i,164,64,16);
            place(dialog,IDC_EQ_SLIDER_FIRST+i,38+66*i,190,30,120);
            place(dialog,IDC_EQ_VALUE_FIRST+i,22+66*i,318,64,18);
        }
        place(dialog,IDC_EQ_STATUS,24,346,392,32);place(dialog,IDC_EQ_RESET,16,390,144,32);
        place(dialog,IDCANCEL,168,390,112,32);place(dialog,IDOK,288,390,128,32);
    }
    EnumChildWindows(dialog,[](HWND child,LPARAM)->BOOL {
        font(child,13);
        wchar_t name[64]{};GetClassNameW(child,name,64);
        if(wcscmp(name,HOTKEY_CLASSW)==0) {
            // The hotkey class adds a non-client edge of its own. Our rounded
            // surface covers the full field; leave no second, unpainted frame.
            SetWindowLongPtrW(child,GWL_STYLE,GetWindowLongPtrW(child,GWL_STYLE)&~WS_BORDER);
            SetWindowLongPtrW(child,GWL_EXSTYLE,GetWindowLongPtrW(child,GWL_EXSTYLE)&~(WS_EX_CLIENTEDGE|WS_EX_STATICEDGE));
            SetWindowPos(child,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
            SetWindowSubclass(child,hotkeyPaint,1,0);
        }
        if(wcscmp(name,TRACKBAR_CLASSW)==0) SetWindowSubclass(child,inputPaint,3,1);
        if(wcscmp(name,L"ComboBox")==0) {
            SetWindowSubclass(child,inputPaint,3,2);SendMessageW(child,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),px(child,28));
        }
        return TRUE;
    },0);
    font(GetDlgItem(dialog,IDC_UI_TITLE),20,FW_SEMIBOLD);
    font(GetDlgItem(dialog,IDC_PANEL_TITLE),15,FW_SEMIBOLD);
    for(int id:{IDC_PANEL_STATUS,IDC_PANEL_NOTE,IDC_PANEL_NOISE,IDC_HOTKEY_HINT,IDC_EQ_HINT,IDC_EQ_STATUS,
        IDC_SETTINGS_DEVICE,IDC_SETTINGS_APPEARANCE,IDC_SETTINGS_LANGUAGE,IDC_SETTINGS_STATUS}) {
        HWND child=GetDlgItem(dialog,id);if(child) { SetPropW(child,L"QuietMuted",reinterpret_cast<HANDLE>(1));font(child,12); }
    }
    SetWindowTextW(GetDlgItem(dialog,IDC_UI_CLOSE),L"×");font(GetDlgItem(dialog,IDC_UI_CLOSE),20);
    font(GetDlgItem(dialog,IDC_PANEL_NOTE),11);
    for(int id:{IDC_PANEL_BATTERY,IDC_PANEL_RIGHT,IDC_PANEL_CASE}) font(GetDlgItem(dialog,id),12,FW_SEMIBOLD);
    for(int i=0;i<6;++i) font(GetDlgItem(dialog,IDC_EQ_NAME_FIRST+i),11);
    if(panel) for(int id:{101,102,103}) SetPropW(GetDlgItem(dialog,id),L"QuietSegment",reinterpret_cast<HANDLE>(1));
    RECT r{};GetWindowRect(dialog,&r);
    HRGN region=CreateRoundRectRgn(0,0,r.right-r.left+1,r.bottom-r.top+1,px(dialog,24),px(dialog,24));
    if(!SetWindowRgn(dialog,region,FALSE)) DeleteObject(region);
    int corner=2;DwmSetWindowAttribute(dialog,33,&corner,sizeof(corner));
    SendMessageW(dialog,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);
}
inline void refresh() {
    if(!graphicsToken) { Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&graphicsToken,&input,nullptr); }
    HIGHCONTRASTW hc{sizeof(hc)};
    highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    dark = preference == 2 || (preference == 0 && light == 0);
    background = dark ? RGB(24,24,27) : RGB(255,255,255);
    surface = dark ? RGB(39,39,42) : RGB(243,244,246);
    foreground = dark ? RGB(244,244,245) : RGB(17,24,39);
    muted = dark ? RGB(161,161,170) : RGB(75,85,99);
    accent = dark ? RGB(59,130,246) : RGB(37,99,235);
    border = dark ? RGB(39,39,42) : RGB(229,231,235);
    if(highContrast) {
        background=GetSysColor(COLOR_WINDOW); surface=GetSysColor(COLOR_BTNFACE);
        foreground=GetSysColor(COLOR_WINDOWTEXT); muted=GetSysColor(COLOR_GRAYTEXT);
        accent=GetSysColor(COLOR_HIGHLIGHT); border=GetSysColor(COLOR_WINDOWTEXT);
    }
    if(backgroundBrush) DeleteObject(backgroundBrush);
    if(surfaceBrush) DeleteObject(surfaceBrush);
    backgroundBrush=CreateSolidBrush(background); surfaceBrush=CreateSolidBrush(surface);
}
inline void apply(HWND dialog) {
    BOOL enabled = dark && !highContrast;
    DwmSetWindowAttribute(dialog, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &enabled, sizeof(enabled));
    EnumChildWindows(dialog, [](HWND child, LPARAM)->BOOL {
        // Documented classic control painting supports explicit application colours.
        wchar_t name[64]{};GetClassNameW(child,name,64);
        bool push=wcscmp(name,L"Button")==0;
        SetWindowTheme(child, highContrast||push ? nullptr : L"", highContrast||push ? nullptr : L"");
        return TRUE;
    },0);
    RedrawWindow(dialog,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
}
inline bool message(HWND dialog, UINT msg, WPARAM w, LPARAM l, INT_PTR& result) {
    // A composited dialog can be repainted without a background-erase request,
    // especially after its children change text or theme. Paint an opaque frame
    // on every WM_PAINT; clearing separately would reintroduce flicker.
    if(msg==WM_ERASEBKGND) { result=TRUE;return true; }
    if(msg==WM_PAINT || msg==WM_PRINTCLIENT) {
        ControlPaint paint(dialog,msg,w);HDC dc=paint.dc();RECT r=paint.rect;
        FillRect(dc,&r,backgroundBrush);rounded(dc,r,background,border,px(dialog,24));
        auto separator=[&](int y,int inset) {
            HPEN pen=CreatePen(PS_SOLID,1,border);auto old=SelectObject(dc,pen);
            MoveToEx(dc,px(dialog,inset),px(dialog,y),nullptr);LineTo(dc,r.right-px(dialog,inset),px(dialog,y));
            SelectObject(dc,old);DeleteObject(pen);
        };
        if(GetDlgItem(dialog,IDC_PANEL_NOISE)) {
            icon(dc,px(dialog,20),px(dialog,22),px(dialog,23),foreground,0);
            separator(120,20);separator(199,0);
            RECT left{},right{};GetWindowRect(GetDlgItem(dialog,101),&left);GetWindowRect(GetDlgItem(dialog,103),&right);
            RECT track{left.left,left.top,right.right,right.bottom};
            MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&track),2);InflateRect(&track,px(dialog,3),px(dialog,3));
            rounded(dc,track,surface,surface,px(dialog,16));
        } else { separator(64,0);if(GetDlgItem(dialog,IDC_HOTKEY_ANC)) separator(436,0); }
        result=TRUE;return true;
    }
    if(msg==WM_LBUTTONDOWN && HIWORD(l)<px(dialog,54)) {
        ReleaseCapture();SendMessageW(dialog,WM_NCLBUTTONDOWN,HTCAPTION,0);result=TRUE;return true;
    }
    if(msg==WM_CTLCOLORDLG || msg==WM_CTLCOLORSTATIC || msg==WM_CTLCOLORBTN ||
        msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX) {
        HDC dc=reinterpret_cast<HDC>(w);
        bool input=msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX;
        HWND control=reinterpret_cast<HWND>(l);
        SetTextColor(dc,IsWindowEnabled(control) && !GetPropW(control,L"QuietMuted") ? foreground : muted);
        SetBkColor(dc,input?surface:background);
        result=reinterpret_cast<INT_PTR>(input?surfaceBrush:backgroundBrush); return true;
    }
    if(msg!=WM_NOTIFY) return false;
    auto header=reinterpret_cast<NMHDR*>(l);
    if(header->code!=NM_CUSTOMDRAW) return false;
    wchar_t name[64]{}; GetClassNameW(header->hwndFrom,name,64);
    auto draw=reinterpret_cast<NMCUSTOMDRAW*>(l);
    auto type=GetWindowLongPtrW(header->hwndFrom,GWL_STYLE)&BS_TYPEMASK;
    if(wcscmp(name,L"Button")==0 && type==BS_AUTOCHECKBOX && draw->dwDrawStage==CDDS_PREPAINT) {
        HWND control=header->hwndFrom;RECT r=draw->rc;FillRect(draw->hdc,&r,backgroundBrush);
        bool checked=SendMessageW(control,BM_GETCHECK,0,0)==BST_CHECKED,enabled=IsWindowEnabled(control)!=FALSE;
        int width=px(dialog,34),height=px(dialog,20);
        RECT toggle{r.right-width,r.top+(r.bottom-r.top-height)/2,r.right,r.top+(r.bottom-r.top+height)/2};
        COLORREF fill=checked&&enabled?accent:surface;
        rounded(draw->hdc,toggle,fill,checked?fill:border,height);
        int pad=px(dialog,3),diameter=height-2*pad;
        int x=checked?toggle.right-pad-diameter:toggle.left+pad;
        RECT knob{x,toggle.top+pad,x+diameter,toggle.top+pad+diameter};
        rounded(draw->hdc,knob,checked?RGB(255,255,255):muted,checked?RGB(255,255,255):muted,diameter);
        auto old=SelectObject(draw->hdc,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)));
        wchar_t label[256]{};GetWindowTextW(control,label,256);r.right=toggle.left-px(dialog,12);
        SetBkMode(draw->hdc,TRANSPARENT);SetTextColor(draw->hdc,enabled?foreground:muted);
        DrawTextW(draw->hdc,label,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(draw->hdc,old);
        if((draw->uItemState&CDIS_FOCUS) && !(SendMessageW(control,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)) DrawFocusRect(draw->hdc,&r);
        result=CDRF_SKIPDEFAULT;SetWindowLongPtrW(dialog,DWLP_MSGRESULT,result);return true;
    }
    if(wcscmp(name,L"Button")==0 && (GetWindowLongPtrW(header->hwndFrom,GWL_STYLE)&BS_TYPEMASK)<=BS_DEFPUSHBUTTON &&
        draw->dwDrawStage==CDDS_PREPAINT) {
        HWND control=header->hwndFrom;
        bool selected=GetPropW(control,L"QuietSelected")!=nullptr;
        bool primary=GetDlgCtrlID(control)==IDOK;
        bool segment=GetPropW(control,L"QuietSegment")!=nullptr;
        bool subtle=GetDlgCtrlID(control)==IDCANCEL || GetDlgCtrlID(control)==IDC_UI_CLOSE ||
            GetDlgCtrlID(control)==IDC_PANEL_SETTINGS || (GetDlgCtrlID(control)>=104 && GetDlgCtrlID(control)<=106) ||
            (GetDlgCtrlID(control)>=IDC_CLEAR_ANC && GetDlgCtrlID(control)<=IDC_CLEAR_OFF);
        bool disabled=!IsWindowEnabled(control);
        COLORREF fill=selected&&!disabled?accent:primary&&!disabled?foreground:subtle?background:surface;
        if(draw->uItemState&(CDIS_SELECTED|CDIS_HOT)) fill=selected?accent:primary?muted:surface;
        RECT r=draw->rc;FillRect(draw->hdc,&r,segment?surfaceBrush:backgroundBrush);
        rounded(draw->hdc,r,fill,fill,px(dialog,12));
        wchar_t label[256]{};GetWindowTextW(control,label,256);
        auto oldFont=SelectObject(draw->hdc,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)));
        SetBkMode(draw->hdc,TRANSPARENT);SetTextColor(draw->hdc,disabled?muted:selected?RGB(255,255,255):primary?background:subtle?muted:foreground);
        int id=GetDlgCtrlID(control);
        if(id==IDC_UI_CLOSE) icon(draw->hdc,(r.right-px(dialog,24))/2,(r.bottom-px(dialog,24))/2,px(dialog,24),muted,2);
        else if(id==IDC_PANEL_SETTINGS) {
            SIZE size{};GetTextExtentPoint32W(draw->hdc,label,static_cast<int>(wcslen(label)),&size);
            int x=(r.right-size.cx-px(dialog,26))/2;
            icon(draw->hdc,x,(r.bottom-px(dialog,18))/2,px(dialog,18),muted,1);
            r.left=x+px(dialog,26);DrawTextW(draw->hdc,label,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        } else if(id==121 || id==132) {
            r.left+=px(dialog,14);DrawTextW(draw->hdc,label,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            icon(draw->hdc,r.right-px(dialog,30),(r.bottom-px(dialog,18))/2,px(dialog,18),muted,3);
        } else DrawTextW(draw->hdc,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(draw->hdc,oldFont);
        if((draw->uItemState&CDIS_FOCUS) && !(SendMessageW(control,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)) {
            InflateRect(&r,-4,-4);DrawFocusRect(draw->hdc,&r);
        }
        result=CDRF_SKIPDEFAULT;
    } else if(wcscmp(name,TRACKBAR_CLASSW)==0 && draw->dwDrawStage==CDDS_PREPAINT) {
        // inputPaint owns the complete slider image, including the thumb.
        // Native incremental drawing must not introduce a second thumb style.
        result=CDRF_SKIPDEFAULT;
    } else return false;
    SetWindowLongPtrW(dialog,DWLP_MSGRESULT,result); return true;
}
inline void cleanup() { DeleteObject(backgroundBrush);DeleteObject(surfaceBrush);if(graphicsToken) Gdiplus::GdiplusShutdown(graphicsToken); }
}
